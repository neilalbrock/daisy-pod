// Tapeworn - lo-fi tape machine for the Daisy Pod (Daisy Seed 3)
//
// Our own take on a deconstructed VHS/cassette deck (after Chase Bliss's
// Generation Loss): every artifact a tape machine adds gets its own control.
// The two knobs are shared across four pages; press the encoder to change
// page (LED 2 shows which). After a page change a knob does nothing until it
// is moved, so values don't jump.
//
//   Page 1 (LED 2 blue)    Knob 1 - wow: slow, wandering pitch drift
//                          Knob 2 - flutter: fast pitch and level quiver
//   Page 2 (LED 2 magenta) Knob 1 - saturate: magnetic hysteresis drive
//                          Knob 2 - failure: crinkles, pops, snags, drops
//   Page 3 (LED 2 yellow)  Knob 1 - dry level (none to unity)
//                          Knob 2 - wet volume (noon is unity, max x2)
//                          (a limiter holds dry + wet under -1 dBFS)
//   Page 4 (LED 2 cyan)    Knob 1 - noise: hiss and hum
//                          Knob 2 - stereo spread: channels fail independently
//   Encoder turn       - tape model (LED 2 flashes its colour; white = flat)
//   Encoder long press - AUX type: stop, fail, filter (LED 1 flashes it)
//   Button 1           - bypass (LED 1: green = active, red = bypassed)
//   Button 1 hold 1 s  - mono in: the left input feeds both channels
//                        (LED 1 flashes cyan for on, dark for off)
//   Button 2           - AUX on/off; hold for momentary. LED 1 shows the AUX
//                        colour while on: blue = stop, orange = fail,
//                        white = filter
//
// Over USB MIDI it talks to the web control surface (web/, `make web`),
// streaming levels, CPU, tape speed and the saturator's loop while the page
// is open.

#include "daisy_pod.h"
#include "bypass_fade.h"
#include "colour.h"
#include "limiter.h"
#include "paged_knobs.h"
#include "press_hold.h"
#include "sdram_timing.h"
#include "tape_engine.h"
#include "util/CpuLoadMeter.h"
#include "web_link.h"
#include "web_view.h"

using namespace daisy;

constexpr size_t kBufferLen = 1 << 17; // 2.7 s: room for the tape stop
float DSY_SDRAM_BSS buffer_l[kBufferLen];
float DSY_SDRAM_BSS buffer_r[kBufferLen];

DaisyPod       hw;
MidiUsbHandler usb_midi;
TapeEngine     tape_engine;
Limiter        limiter; // on dry + wet, so turning both up doesn't clip
CpuLoadMeter   cpu_meter;

constexpr int kNumPages = 4;
constexpr int kNumKnobs = 2;

// Raw knob positions (0..1) per page, initial values chosen to sound sensible.
PagedKnobs<kNumPages, kNumKnobs> pages({
    {0.3f, 0.3f},   // wow, flutter
    {0.35f, 0.15f}, // saturate, failure
    {0.0f, 0.5f},   // dry none, volume unity
    {0.2f, 0.0f},   // noise, spread
});

int             model    = 5; // VHS 2nd gen
TapeEngine::Aux aux_type = TapeEngine::Aux::kStop;
bool            aux_on   = false;
bool            bypass   = false;
bool            mono_in  = false;

constexpr uint32_t kMomentaryMs = 400; // button 2: held this long = momentary

PressHold  encoder_press(600);  // hold: AUX type
PressHold  button1_press(1000); // hold: mono in
BypassFade bypass_fade;
uint32_t   aux_held_ms = 0; // how long button 2 has been held

// Read by the main loop for the LEDs: flash until these times (ms).
volatile uint32_t model_flash_until = 0;
volatile uint32_t aux_flash_until   = 0;
volatile uint32_t mono_flash_until  = 0;

// ---------------------------------------------------------------- Web link
//
// The page in web/ (see web_view.h). It sets values through SysEx, read in
// the audio callback; the main loop sends it what it needs.

using namespace web_view;

uint8_t DSY_SDRAM_BSS web_buffer[weblink::kMaxMessage];
weblink::Session      web;
weblink::Writer       web_out;
int                   web_state[kNumParams]; // values in the last state sent

// The audio callback appends to these rings; the main loop reads what's new
// for each frame, so the callback never does more than append. The rings
// are powers of two, longer than a frame takes.
constexpr uint32_t  kTraceRing = 64;
constexpr uint32_t  kXYRing    = 128;
float DSY_SDRAM_BSS trace_ring[kTraceRing][kNumTraces];
float DSY_SDRAM_BSS xy_ring[kXYRing][2];
volatile uint32_t   trace_written = 0, xy_written = 0; // points so far
uint32_t            trace_read    = 0;                 // main loop: points sent
float               trace_sum[2] = {}, trace_min[2] = {1.0f, 1.0f};
int                 trace_samples = 0, trace_period = 48;

volatile float peaks[3]    = {}; // in, wet, out
volatile bool  reset_peaks = false;

Telemetry DSY_SDRAM_BSS telemetry; // the main loop's copy

void SetModel(int m, uint32_t now_ms)
{
    model             = m < 0 ? 0 : (m >= TapeModel::kNumModels ? TapeModel::kNumModels - 1 : m);
    model_flash_until = now_ms + 1000;
}

// A value from the page, in the audio callback.
void SetParam(int index, int raw, uint32_t now_ms)
{
    switch(index)
    {
        case kParamModel: SetModel(raw, now_ms); break;
        case kParamAuxType:
            aux_type        = (TapeEngine::Aux)(raw % TapeEngine::kNumAux);
            aux_flash_until = now_ms + 600;
            break;
        case kParamAux: aux_on = raw != 0; break;
        case kParamBypass: bypass = raw != 0; break;
        case kParamMono: mono_in = raw != 0; break;
        default:
            if(index >= kParamWow && index <= kParamSpread)
                pages.Set((index - 1) / 2, (index - 1) % 2, raw / 16383.0f);
            break;
    }
}

// A value for the page, in the main loop.
int ParamRaw(int index)
{
    switch(index)
    {
        case kParamModel: return model;
        case kParamAuxType: return (int)aux_type;
        case kParamAux: return aux_on;
        case kParamBypass: return bypass;
        case kParamMono: return mono_in;
        default:
            return (int)(pages.Value((index - 1) / 2, (index - 1) % 2) * 16383.0f + 0.5f);
    }
}

// Once per block, in the audio callback: the saturator every block, and a
// trace point every millisecond, averaging speed and keeping the lowest
// level over it.
void Collect(size_t size)
{
    const TapeEngine::Probe &p = tape_engine.GetProbe();
    float *xy                  = xy_ring[xy_written & (kXYRing - 1)];
    xy[0]                      = p.sat_in;
    xy[1]                      = p.sat_out;
    xy_written                 = xy_written + 1;

    for(int c = 0; c < 2; c++)
    {
        trace_sum[c] += p.speed[c];
        trace_min[c] = fminf(trace_min[c], p.level[c]);
    }
    trace_samples += (int)size;
    if(trace_samples >= trace_period)
    {
        int    blocks       = trace_samples / (int)size;
        float *point        = trace_ring[trace_written & (kTraceRing - 1)];
        point[kTraceSpeedL] = SpeedCents(trace_sum[0] / blocks);
        point[kTraceSpeedR] = SpeedCents(trace_sum[1] / blocks);
        point[kTraceLevelL] = 100.0f * trace_min[0];
        point[kTraceLevelR] = 100.0f * trace_min[1];
        trace_written       = trace_written + 1; // after the point is in place
        trace_sum[0] = trace_sum[1] = 0.0f;
        trace_min[0] = trace_min[1] = 1.0f;
        trace_samples = 0;
    }
}

// In the main loop: what's new since the last frame. The callback only
// overwrites ring entries far older than these.
void ReadTelemetry()
{
    Telemetry &t = telemetry;
    for(int i = 0; i < 3; i++)
        t.peaks[i] = peaks[i];
    reset_peaks = true;

    uint32_t written = trace_written;
    uint32_t n       = written - trace_read;
    if(n > (uint32_t)kTracePoints)
        n = kTracePoints; // the page fell behind: skip the oldest
    for(uint32_t i = 0; i < n; i++)
    {
        const float *src = trace_ring[(written - n + i) & (kTraceRing - 1)];
        for(int k = 0; k < kNumTraces; k++)
            t.trace[i][k] = src[k];
    }
    t.num_points = (int)n;
    trace_read   = written;

    written = xy_written;
    for(uint32_t i = 0; i < (uint32_t)kXYPoints; i++)
    {
        const float *src = xy_ring[(written - kXYPoints + i) & (kXYRing - 1)];
        t.xy[i][0]       = src[0];
        t.xy[i][1]       = src[1];
    }
}

// From the main loop: sends the page the next message it needs, if any.
void SendWeb(uint32_t now_ms)
{
    int  raw[kNumParams];
    bool changed = false;
    for(int i = 0; i < kNumParams; i++)
    {
        raw[i] = ParamRaw(i);
        if(raw[i] != web_state[i])
            changed = true;
    }

    weblink::Writer &w     = web_out;
    int              param = 0;
    switch(web.Next(now_ms, changed, &param))
    {
        case weblink::Session::Send::kHello: WriteHello(w, hw.AudioSampleRate()); break;
        case weblink::Session::Send::kParam: weblink::WriteParam(w, param, kParams[param]); break;
        case weblink::Session::Send::kState:
            w.Begin(weblink::Msg::kState);
            w.U7(kNumParams);
            for(int i = 0; i < kNumParams; i++)
            {
                w.U14((uint32_t)raw[i]);
                web_state[i] = raw[i];
            }
            break;
        case weblink::Session::Send::kFrame:
            ReadTelemetry();
            WriteFrame(w, telemetry, cpu_meter.GetAvgCpuLoad(), cpu_meter.GetMaxCpuLoad());
            cpu_meter.Reset();
            web.TakeRestart(); // no buffer overview to restart
            break;
        default: return;
    }
    size_t size = w.End();
    if(size > 0)
        usb_midi.SendMessage(w.Data(), size);
}

// ---------------------------------------------------------------- Audio

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    cpu_meter.OnBlockStart();
    hw.ProcessAllControls();
    uint32_t now_ms = System::GetNow();

    // The web page's settings, over USB MIDI SysEx.
    usb_midi.Listen();
    while(usb_midi.HasEvents())
    {
        MidiEvent e = usb_midi.PopEvent();
        weblink::Command c;
        if(e.type == SystemCommon && e.sc_type == SystemExclusive
           && web.Receive(e.sysex_data, e.sysex_message_len, now_ms, &c))
            SetParam(c.index, c.raw, now_ms);
    }

    // Button 1: a tap toggles bypass on release, a 1 s hold toggles mono in.
    PressHold::Event b1 = button1_press.Update(hw.button1.Pressed(),
                                               hw.button1.TimeHeldMs());
    if(b1 == PressHold::Event::kHold)
    {
        mono_in          = !mono_in;
        mono_flash_until = now_ms + 500;
    }
    else if(b1 == PressHold::Event::kTap)
        bypass = !bypass;

    // Button 2: press toggles AUX; releasing after a hold turns it back off.
    if(hw.button2.RisingEdge())
        aux_on = !aux_on;
    if(hw.button2.Pressed())
        aux_held_ms = hw.button2.TimeHeldMs();
    if(hw.button2.FallingEdge() && aux_held_ms >= kMomentaryMs)
        aux_on = false;

    int inc = hw.encoder.Increment();
    if(inc != 0)
        SetModel(model + inc, now_ms);

    const DaisyPod::Knob knob_ids[kNumKnobs] = {DaisyPod::KNOB_1, DaisyPod::KNOB_2};
    float                knob[kNumKnobs];
    for(int k = 0; k < kNumKnobs; k++)
        knob[k] = hw.GetKnobValue(knob_ids[k]);

    // Long press cycles the AUX type as soon as the hold is long enough; a
    // short press changes page on release.
    PressHold::Event enc = encoder_press.Update(hw.encoder.Pressed(),
                                                hw.encoder.TimeHeldMs());
    if(enc == PressHold::Event::kHold)
    {
        aux_type = (TapeEngine::Aux)(((int)aux_type + 1) % TapeEngine::kNumAux);
        aux_flash_until = now_ms + 600;
    }
    else if(enc == PressHold::Event::kTap)
        pages.NextPage(knob);
    pages.Update(knob);

    TapeEngine::Params p;
    p.wow      = pages.Value(0, 0);
    p.flutter  = pages.Value(0, 1);
    p.saturate = pages.Value(1, 0);
    p.failure  = pages.Value(1, 1);
    p.noise    = pages.Value(3, 0);
    p.spread   = pages.Value(3, 1);
    p.model    = model;
    p.aux      = aux_type;
    p.aux_on   = aux_on;
    tape_engine.SetParams(p);

    float dry    = pages.Value(2, 0) * pages.Value(2, 0);
    float volume = pages.Value(2, 1) * 2.0f;

    // Peaks for the web page's meters, since it last read them.
    if(reset_peaks)
    {
        peaks[0] = peaks[1] = peaks[2] = 0.0f;
        reset_peaks                    = false;
    }
    float peak_in = peaks[0], peak_wet = peaks[1], peak_out = peaks[2];

    for(size_t i = 0; i < size; i++)
    {
        float in_l = in[0][i];
        float in_r = mono_in ? in_l : in[1][i];

        // Keep the tape running in bypass so it's up to date when you return.
        float wet_l, wet_r;
        tape_engine.Process(in_l, in_r, &wet_l, &wet_r);

        float fx_l = wet_l * volume + in_l * dry;
        float fx_r = wet_r * volume + in_r * dry;
        limiter.Process(&fx_l, &fx_r);
        bypass_fade.Process(bypass, in_l, in_r, &fx_l, &fx_r);
        out[0][i] = fx_l;
        out[1][i] = fx_r;

        peak_in  = fmaxf(peak_in, fmaxf(fabsf(in_l), fabsf(in_r)));
        peak_wet = fmaxf(peak_wet, fmaxf(fabsf(wet_l), fabsf(wet_r)));
        peak_out = fmaxf(peak_out, fmaxf(fabsf(fx_l), fabsf(fx_r)));
    }
    peaks[0] = peak_in;
    peaks[1] = peak_wet;
    peaks[2] = peak_out;

    Collect(size);
    cpu_meter.OnBlockEnd();
}

int main(void)
{
    hw.Init(true); // 480 MHz: the hysteresis solver needs the headroom
    TuneSdramTimings();
    hw.SetAudioBlockSize(4);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    tape_engine.Init(hw.AudioSampleRate(), buffer_l, buffer_r, kBufferLen);
    limiter.Init(hw.AudioSampleRate());
    bypass_fade.Init(hw.AudioSampleRate());
    cpu_meter.Init(hw.AudioSampleRate(), hw.AudioBlockSize());
    web.Init(kNumParams);
    web_out.Init(web_buffer);

    MidiUsbHandler::Config usb_cfg;
    usb_cfg.transport_config.periph = MidiUsbTransport::Config::INTERNAL;
    // A long SysEx message to the web page can still be going out when the
    // next one is ready; wait up to 2 ms for it (in the main loop).
    usb_cfg.transport_config.tx_retry_count = 20;
    usb_midi.Init(usb_cfg);
    usb_midi.StartReceive();
    // libDaisy gives USB the same interrupt priority as the audio DMA, so
    // while the page streams, a USB interrupt could delay the audio callback
    // past its deadline. Let audio interrupt USB instead.
    HAL_NVIC_SetPriority(OTG_FS_IRQn, 1, 0);
    HAL_NVIC_SetPriority(OTG_FS_EP1_OUT_IRQn, 1, 0);
    HAL_NVIC_SetPriority(OTG_FS_EP1_IN_IRQn, 1, 0);
    trace_period = (int)(hw.AudioSampleRate() * kTracePeriodMs / 1000.0f);

    // Every knob starts "not live" so the defaults above hold until a knob moves.
    hw.StartAdc();
    System::Delay(10); // let the ADC settle before taking the reference reading
    hw.ProcessAnalogControls();
    pages.Latch({hw.GetKnobValue(DaisyPod::KNOB_1), hw.GetKnobValue(DaisyPod::KNOB_2)});

    hw.StartAudio(AudioCallback);

    const float page_colours[kNumPages][3] = {
        {0.0f, 0.0f, 1.0f}, // blue
        {1.0f, 0.0f, 1.0f}, // magenta
        {1.0f, 0.7f, 0.0f}, // yellow
        {0.0f, 1.0f, 1.0f}, // cyan
    };
    const float aux_colours[TapeEngine::kNumAux][3] = {
        {0.0f, 0.0f, 1.0f},  // stop: blue
        {1.0f, 0.3f, 0.0f},  // fail: orange
        {1.0f, 1.0f, 1.0f},  // filter: white
    };
    while(1)
    {
        uint32_t    now = System::GetNow();
        const float *a  = aux_colours[(int)aux_type];

        // LED 1: mono in / AUX type flashes, then bypass, AUX, or active.
        if(now < mono_flash_until)
        {
            if(mono_in)
                hw.led1.Set(0.0f, 1.0f, 1.0f);
            else
                hw.led1.Set(0.0f, 0.0f, 0.0f);
        }
        else if(now < aux_flash_until)
        {
            bool lit = (now / 75) % 2;
            hw.led1.Set(lit ? a[0] : 0.0f, lit ? a[1] : 0.0f, lit ? a[2] : 0.0f);
        }
        else if(bypass)
            hw.led1.Set(1.0f, 0.0f, 0.0f);
        else if(aux_on)
            hw.led1.Set(a[0], a[1], a[2]);
        else
            hw.led1.Set(0.0f, 1.0f, 0.0f);

        // LED 2: page colour, or the model's colour just after a change.
        if(now < model_flash_until)
        {
            float r = 1.0f, g = 1.0f, b = 1.0f; // model 0, flat: white
            if(model > 0)
                Hue(0.8f * (model - 1) / (TapeModel::kNumModels - 2), &r, &g, &b);
            hw.led2.Set(r, g, b);
        }
        else
        {
            const float *c = page_colours[pages.Page()];
            hw.led2.Set(c[0], c[1], c[2]);
        }
        hw.UpdateLeds();

        SendWeb(now);
        System::Delay(10);
    }
}

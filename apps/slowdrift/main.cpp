// Slowdrift - a stereo ambient reverb for the Daisy Pod (Daisy Seed 3)
//
// Our own take on Walrus Audio's Slöer: a modulated plate with five modes,
// each giving the X control its own job, and Stretch, which lowers the
// reverb's sample rate for longer, darker, grittier tails. The two knobs
// are shared across four pages; press the encoder to change page (LED 2
// shows which). After a page change a knob does nothing until it is moved,
// so values don't jump. Turning the encoder sets the page's choice.
//
//   Page 1 (LED 2 blue)    Knob 1 - Decay: 0.3 s to 20 s
//                          Knob 2 - Mix: dry to wet, equal power
//                          Encoder - Mode: Dream, Light, Rain, Dark, Rise
//   Page 2 (LED 2 magenta) Knob 1 - Filter: the mode's low-pass, dark to bright
//                          Knob 2 - Depth: modulation, none to seasick
//                          Encoder - Waveshape: sine, warp, sink, square, random
//   Page 3 (LED 2 yellow)  Knob 1 - X, by mode:
//                            Dream: vibrato on the reverb
//                            Light: octave-up shimmer
//                            Rain:  diffusion, echoes to wash
//                            Dark:  an octave down into the reverb
//                            Rise:  swell time, 0.1 s to 4 s
//                          Knob 2 - Stretch: the reverb's rate, 48k down to 12k
//                          Encoder - Width: narrow, medium, wide
//   Page 4 (LED 2 cyan)    Knob 1 - Rate: the LFO, 0.05 Hz to 10 Hz (LED 1
//                                   pulses with it on this page)
//                          Knob 2 - Pan: the wet to one side and the dry to
//                                   the other; noon is centred
//                          Encoder - Trails: on or off
//   (a limiter holds dry + wet under -1 dBFS)
//
//   Encoder long press - mono in: the left input feeds both channels
//                        (LED 1 flashes cyan for on, dark for off)
//   Button 1           - on/off. Pressed while off, it comes on at once; let
//                        go within 0.4 s and it stays on, hold it longer and
//                        it goes off again on release (smart bypass)
//   Button 2           - Sustain: the decay stretches to 60 s while held.
//                        In Dream, a press freezes the pad (the input is
//                        shut out and the tail held); another lets it go
//
// LED 1 shows the mode's colour while on and is dark while off. It glows
// white while Sustain or Freeze holds the tail. Turning the encoder flashes
// LED 2 with the choice: the mode's colour, a hue per waveshape, red, green
// or blue for the widths, and green or red for trails on or off.
//
// MIDI CCs on any channel, from the TRS input or USB (see README.md):
//
//   14 Decay   15 Mix   16 Filter   17 Depth   18 X   19 Stretch
//   20 Rate    21 Pan
//   22 Mode    23 Waveshape   24 Width (0-127 split evenly between them)
//   25 Trails  (64 and up: on)
//   64 Sustain pedal: as button 2
//   102 On/off   103 Freeze (Dream)   104 Mono in   (64 and up: on)
//
// Over USB MIDI it also talks to the web control surface (web/, `make
// web`), streaming levels, CPU, the LFO, the reverb's input gain and the
// decay time while the page is open. Nothing is saved: it all starts from
// the defaults at power-up.

#include "daisy_pod.h"
#include "colour.h"
#include "limiter.h"
#include "paged_knobs.h"
#include "press_hold.h"
#include "slow_engine.h"
#include "util/CpuLoadMeter.h"
#include "web_link.h"
#include "web_view.h"

using namespace daisy;
using namespace web_view;

// The plate reads about 40 taps a tick, so its lines go in the 256 KB of
// RAM_D2, which reads far faster than SDRAM. BOOT_SRAM apps leave RAM_D2
// to the heap, so the linker script's .heap section is the way in. Leave
// room: malloc's heap starts where .heap ends and has no limit check, and
// the USB stack uses it.
float __attribute__((section(".heap"))) reverb_mem[SlowEngine::kFloats];

uint8_t DSY_SDRAM_BSS web_buffer[weblink::kMaxMessage];

DaisyPod       hw;
MidiUsbHandler usb_midi;
SlowEngine     engine;
Limiter        limiter; // on dry + wet, so a loud wet signal doesn't clip
CpuLoadMeter   cpu_meter;

constexpr int kNumPages = 4;
constexpr int kNumKnobs = 2;

// Raw knob positions (0..1) per page, initial values chosen to sound sensible.
PagedKnobs<kNumPages, kNumKnobs> pages({
    {0.55f, 0.4f}, // decay 3 s, mix
    {0.6f, 0.3f},  // filter, depth
    {0.4f, 0.0f},  // x, no stretch
    {0.4f, 0.5f},  // rate 0.4 Hz, pan centred
});

// ---------------------------------------------------------------- State
//
// What the Pod and the web page set. Only the audio callback writes it;
// the main loop reads it for the LEDs and the page.

// Each page's encoder choice: mode, waveshape, width, trails.
constexpr int kChoiceCounts[kNumPages] = {SlowEngine::kNumModes, Lfo::kNumShapes,
                                          SlowEngine::kNumWidths, 2};
int  choices[kNumPages] = {SlowEngine::kDream, Lfo::kSine, SlowEngine::kWide, 1};
bool active  = true;
bool sustain = false;
bool freeze  = false;
bool mono_in = false;

constexpr uint32_t kMomentaryMs = 400; // button 1 held this long while off: smart bypass

PressHold encoder_press(600); // hold: mono in
bool      momentary   = false; // button 1 turned it on with this press
uint32_t  b1_held_ms  = 0;

// Read by the main loop for the LEDs: flash until these times (ms).
volatile uint32_t choice_flash_until = 0;
volatile uint32_t mono_flash_until   = 0;
volatile float    lfo_now            = 0.0f;

// ---------------------------------------------------------------- Web link
//
// The page in web/ (see web_view.h). It sets values through SysEx, read in
// the audio callback; the main loop sends it what it needs.

weblink::Session web;
weblink::Writer  web_out;
int              web_state[kNumParams]; // values in the last state sent

// The audio callback appends to this ring; the main loop reads what's new
// for each frame. A power of two, longer than a frame takes.
constexpr uint32_t  kTraceRing = 64;
float DSY_SDRAM_BSS trace_ring[kTraceRing][kNumTraces];
volatile uint32_t   trace_written = 0; // points so far
uint32_t            trace_read    = 0; // main loop: points sent
float               input_min     = 1.0f;
int                 trace_samples = 0, trace_period = 48;

volatile float peaks[3]    = {}; // in, wet, out
volatile bool  reset_peaks = false;

Telemetry DSY_SDRAM_BSS telemetry; // the main loop's copy

// LED 2 flashes the new choice if its page is showing.
void SetChoice(int page, int value, uint32_t now_ms)
{
    int n         = kChoiceCounts[page];
    choices[page] = value < 0 ? 0 : (value >= n ? n - 1 : value);
    if(page == pages.Page())
        choice_flash_until = now_ms + 1000;
}

// Button 2 or a sustain pedal: in Dream a press freezes the pad or lets it
// go; in the other modes, Sustain while it's down.
void SustainSwitch(bool down)
{
    if(choices[0] == SlowEngine::kDream)
    {
        if(down)
            freeze = !freeze;
    }
    else
        sustain = down;
}

// A value from the page, in the audio callback.
void SetParam(int index, int raw, uint32_t now_ms)
{
    switch(index)
    {
        case kParamMode: SetChoice(0, raw, now_ms); break;
        case kParamShape: SetChoice(1, raw, now_ms); break;
        case kParamWidth: SetChoice(2, raw, now_ms); break;
        case kParamTrails: SetChoice(3, raw != 0, now_ms); break;
        case kParamActive: active = raw != 0; break;
        case kParamSustain: sustain = raw != 0; break;
        case kParamFreeze: freeze = raw != 0; break;
        case kParamMono: mono_in = raw != 0; break;
        default:
            if(index < kNumKnobParams)
                pages.Set(index / 2, index % 2, raw / 16383.0f);
            break;
    }
}

// A value for the page, in the main loop.
int ParamRaw(int index)
{
    switch(index)
    {
        case kParamMode: return choices[0];
        case kParamShape: return choices[1];
        case kParamWidth: return choices[2];
        case kParamTrails: return choices[3];
        case kParamActive: return active;
        case kParamSustain: return sustain;
        case kParamFreeze: return freeze;
        case kParamMono: return mono_in;
        default: return (int)(pages.Value(index / 2, index % 2) * 16383.0f + 0.5f);
    }
}

// Once per block, in the audio callback: a trace point every millisecond,
// keeping the lowest input gain over it so short dips show.
void Collect(size_t size)
{
    SlowEngine::Probe p = engine.GetProbe();
    lfo_now             = p.lfo;
    input_min           = fminf(input_min, p.input);
    trace_samples += (int)size;
    if(trace_samples >= trace_period)
    {
        float *point       = trace_ring[trace_written & (kTraceRing - 1)];
        point[kTraceLfo]   = 100.0f * p.lfo;
        point[kTraceInput] = 100.0f * input_min;
        point[kTraceDecay] = p.rt60;
        trace_written      = trace_written + 1; // after the point is in place
        input_min          = 1.0f;
        trace_samples      = 0;
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

// ---------------------------------------------------------------- MIDI
//
// CCs on any channel, from the TRS input or USB. The knobs take over as
// from the web page: the Pod's knob lets go until it's moved. Choices
// split 0-127 evenly, so a controller's knob sweeps through them.

bool pedal_down = false; // CC 64, for its edges

void HandleCc(uint8_t cc, uint8_t value, uint32_t now_ms)
{
    bool on = value >= 64;
    switch(cc)
    {
        case 22: SetChoice(0, value * SlowEngine::kNumModes / 128, now_ms); break;
        case 23: SetChoice(1, value * Lfo::kNumShapes / 128, now_ms); break;
        case 24: SetChoice(2, value * SlowEngine::kNumWidths / 128, now_ms); break;
        case 25: SetChoice(3, on, now_ms); break;
        case 64:
            if(on != pedal_down)
                SustainSwitch(on);
            pedal_down = on;
            break;
        case 102:
            active    = on;
            momentary = false;
            break;
        case 103: freeze = on; break; // Dream only
        case 104: mono_in = on; break;
        default:
            // 14-21: the knobs, page by page
            if(cc >= 14 && cc < 14 + kNumKnobParams)
                pages.Set((cc - 14) / 2, (cc - 14) % 2, value / 127.0f);
            break;
    }
}

void HandleMidi(MidiEvent e, uint32_t now_ms)
{
    switch(e.type)
    {
        case ControlChange:
        {
            ControlChangeEvent cc = e.AsControlChange();
            HandleCc(cc.control_number, cc.value, now_ms);
            break;
        }
        case SystemCommon:
            // The web page's settings.
            if(e.sc_type == SystemExclusive)
            {
                weblink::Command c;
                if(web.Receive(e.sysex_data, e.sysex_message_len, now_ms, &c))
                    SetParam(c.index, c.raw, now_ms);
            }
            break;
        default: break;
    }
}

// ---------------------------------------------------------------- Audio

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    cpu_meter.OnBlockStart();
    hw.ProcessAllControls();
    uint32_t now_ms = System::GetNow();

    hw.midi.Listen();
    usb_midi.Listen();
    while(hw.midi.HasEvents())
        HandleMidi(hw.midi.PopEvent(), now_ms);
    while(usb_midi.HasEvents())
        HandleMidi(usb_midi.PopEvent(), now_ms);

    // Button 1: pressed while off, on at once, and off again on release if
    // held (smart bypass); pressed while on, off on release.
    if(hw.button1.RisingEdge())
    {
        momentary = !active;
        active    = true;
    }
    if(hw.button1.Pressed())
        b1_held_ms = hw.button1.TimeHeldMs();
    if(hw.button1.FallingEdge())
    {
        if(!momentary || b1_held_ms >= kMomentaryMs)
            active = false;
        momentary = false;
    }

    // Button 2: Sustain while held, or in Dream, a press freezes the pad
    // or lets it go. Freeze is Dream's alone, and Sustain the others'.
    if(hw.button2.RisingEdge())
        SustainSwitch(true);
    if(hw.button2.FallingEdge())
        SustainSwitch(false);
    if(choices[0] == SlowEngine::kDream)
        sustain = false;
    else
        freeze = false;

    const DaisyPod::Knob knob_ids[kNumKnobs] = {DaisyPod::KNOB_1, DaisyPod::KNOB_2};
    float                knob[kNumKnobs];
    for(int k = 0; k < kNumKnobs; k++)
        knob[k] = hw.GetKnobValue(knob_ids[k]);

    // Long press toggles mono in as soon as the hold is long enough; a short
    // press changes page on release.
    PressHold::Event enc = encoder_press.Update(hw.encoder.Pressed(),
                                                hw.encoder.TimeHeldMs());
    if(enc == PressHold::Event::kHold)
    {
        mono_in          = !mono_in;
        mono_flash_until = now_ms + 500;
    }
    else if(enc == PressHold::Event::kTap)
    {
        pages.NextPage(knob);
        choice_flash_until = 0;
    }
    pages.Update(knob);

    int inc = hw.encoder.Increment();
    if(inc != 0)
        SetChoice(pages.Page(), choices[pages.Page()] + inc, now_ms);

    SlowEngine::Params p;
    p.decay   = pages.Value(0, 0);
    p.mix     = pages.Value(0, 1);
    p.filter  = pages.Value(1, 0);
    p.depth   = pages.Value(1, 1);
    p.x       = pages.Value(2, 0);
    p.stretch = pages.Value(2, 1);
    p.rate    = pages.Value(3, 0);
    p.pan     = pages.Value(3, 1);
    p.mode    = choices[0];
    p.shape   = choices[1];
    p.width   = choices[2];
    p.trails  = choices[3] != 0;
    p.active  = active;
    p.sustain = sustain;
    p.freeze  = freeze;
    engine.SetParams(p);

    // Peaks for the web page's meters, since it last read them.
    if(reset_peaks)
    {
        peaks[0] = peaks[1] = peaks[2] = 0.0f;
        engine.TakeWetPeak();
        reset_peaks = false;
    }
    float peak_in = peaks[0], peak_out = peaks[2];

    for(size_t i = 0; i < size; i++)
    {
        float in_l = in[0][i];
        float in_r = mono_in ? in_l : in[1][i];

        float l, r;
        engine.Process(in_l, in_r, &l, &r);
        limiter.Process(&l, &r);
        out[0][i] = l;
        out[1][i] = r;

        peak_in  = fmaxf(peak_in, fmaxf(fabsf(in_l), fabsf(in_r)));
        peak_out = fmaxf(peak_out, fmaxf(fabsf(l), fabsf(r)));
    }
    peaks[0] = peak_in;
    peaks[1] = fmaxf(peaks[1], engine.TakeWetPeak());
    peaks[2] = peak_out;

    Collect(size);
    cpu_meter.OnBlockEnd();
}

int main(void)
{
    hw.Init(true); // 480 MHz
    hw.SetAudioBlockSize(4);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    engine.Init(hw.AudioSampleRate(), hw.AudioBlockSize(), reverb_mem);
    limiter.Init(hw.AudioSampleRate());
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
    hw.midi.StartReceive();
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
    const float mode_colours[SlowEngine::kNumModes][3] = {
        {0.5f, 0.1f, 1.0f}, // Dream: violet
        {1.0f, 1.0f, 0.6f}, // Light: pale gold
        {0.0f, 0.6f, 1.0f}, // Rain: sky blue
        {1.0f, 0.0f, 0.1f}, // Dark: red
        {1.0f, 0.4f, 0.0f}, // Rise: orange
    };
    const float position_colours[3][3] = {
        {1.0f, 0.0f, 0.0f}, // red
        {0.0f, 1.0f, 0.0f}, // green
        {0.0f, 0.0f, 1.0f}, // blue
    };
    while(1)
    {
        uint32_t now  = System::GetNow();
        int      page = pages.Page();
        float    r, g, b;

        // LED 1: the mono in flash, else dark while off, else the mode's
        // colour, glowing white while the tail is held, and pulsing with
        // the LFO on the Rate page.
        if(now < mono_flash_until)
        {
            r = 0.0f;
            g = b = mono_in ? 1.0f : 0.0f;
        }
        else if(!active)
            r = g = b = 0.0f;
        else
        {
            const float *c = mode_colours[choices[0]];
            float held     = sustain || freeze ? 0.6f : 0.0f;
            r              = c[0] + held * (1.0f - c[0]);
            g              = c[1] + held * (1.0f - c[1]);
            b              = c[2] + held * (1.0f - c[2]);
            if(page == 3)
            {
                float level = 0.1f + 0.9f * (0.5f + 0.5f * lfo_now);
                r *= level;
                g *= level;
                b *= level;
            }
        }
        hw.led1.Set(r, g, b);

        // LED 2: page colour, or the page's choice just after a change.
        if(now < choice_flash_until)
        {
            int v = choices[page];
            switch(page)
            {
                case 0:
                    r = mode_colours[v][0];
                    g = mode_colours[v][1];
                    b = mode_colours[v][2];
                    break;
                case 1: Hue((float)v / Lfo::kNumShapes, &r, &g, &b); break;
                case 2:
                    r = position_colours[v][0];
                    g = position_colours[v][1];
                    b = position_colours[v][2];
                    break;
                default:
                    r = v ? 0.0f : 1.0f;
                    g = v ? 1.0f : 0.0f;
                    b = 0.0f;
                    break;
            }
        }
        else
        {
            r = page_colours[page][0];
            g = page_colours[page][1];
            b = page_colours[page][2];
        }
        hw.led2.Set(r, g, b);
        hw.UpdateLeds();

        SendWeb(now);
        System::Delay(10);
    }
}

// Stormsea - a stereo reverb with a storm on top, for the Daisy Pod (Daisy Seed 3)
//
// Our own design, after Oceanus Procellarum, the Ocean of Storms on the
// Moon. With the storm down it's a clean, lush reverb (a feedback delay
// network) whose tails run from half a second to forever. This is stage 1
// of the roadmap in README.md, the calm sea; the storm comes next.
//
// The two knobs are shared across three pages; press the encoder to change
// page (LED 2 shows which). After a page change a knob does nothing until
// it is moved, so values don't jump. Turning the encoder sets the page's
// choice, if it has one.
//
//   Page 1 Sea (LED 2 blue)      Knob 1 - Decay: 0.5 s to 60 s, then ∞ over
//                                         the last stretch
//                                Knob 2 - Mix: dry to wet, equal power
//                                Encoder - Width: narrow, medium, wide
//   Page 2 Shape (LED 2 cyan)    Knob 1 - Size: a hall to a vast open sea
//                                Knob 2 - Tone: dark to bright
//                                Encoder - Predelay: 0, 15, 30, 60, 120, 240 ms
//   Page 3 Motion (LED 2 green)  Knob 1 - Depth: the lines' slow swing
//                                Knob 2 - Tide: slow swells of level and position
//   (a limiter holds dry + wet under -1 dBFS)
//
//   Page 4 (LED 2: the slot) Presets. Encoder turn - select a slot, loading
//                            its preset; an empty one keeps the sound.
//                            Encoder long press - save to the slot. LED 2
//                            shows the slot's colour: dim if empty,
//                            blinking off while modified, flickering white
//                            on a save. The knobs do nothing here.
//
//   Encoder long press - mono in: the left input feeds both channels
//                        (LED 1 flashes cyan for on, dark for off)
//   Button 1 tap       - on/off (holding it will be Calm, with the storm)
//   Button 2 tap       - Freeze: holds the sea forever, until tapped again
//                        (holding it will be Squall, with the storm)
//
// LED 1 is dark while off, a calm deep blue while on, and pale moonlight
// white while frozen. Turning the encoder flashes LED 2 with the choice:
// red, green or blue for the widths, and the predelay as cyan, dim to
// bright.
//
// MIDI CCs on any channel, from the TRS input or USB (see README.md):
//
//   14 Decay   15 Mix   16 Size   17 Tone   18 Depth   19 Tide
//   28 Width   29 Predelay   (0-127 split evenly between them)
//   64 Sustain pedal: Freeze while down
//   102 On/off   103 Freeze   104 Mono in   107 Trails   108 Kill dry
//   (64 and up: on)
//   Program change 1-8 (sent as 0-7) selects preset slot 1-8
//
// Over USB MIDI it also talks to the web control surface (web/, `make
// web`), streaming levels, CPU, the sea's level, Tide's swell and the decay
// time while the page is open, with the presets.
//
// Presets (common/presets.h) hold the knobs, width and predelay. Mono in,
// trails and kill dry are saved on their own a few seconds after they
// change. At power-up they come back, then the preset last selected.
//
// Hold both buttons while powering up (both LEDs flash white) for a factory
// reset: this app's presets and saved settings are erased. The web page
// does the same while running.

#include "daisy_pod.h"
#include "factory_reset.h"
#include "limiter.h"
#include "paged_knobs.h"
#include "presets.h"
#include "press_hold.h"
#include "qspi_flash.h"
#include "sdram_timing.h"
#include "storm_engine.h"
#include "util/CpuLoadMeter.h"
#include "web_link.h"
#include "web_view.h"

using namespace daisy;
using namespace web_view;

// The sea reads about 24 times a sample, so its lines go in the 256 KB of
// RAM_D2, which reads far faster than SDRAM. BOOT_SRAM apps leave RAM_D2
// to the heap, so the linker script's .heap section is the way in. Leave
// room: malloc's heap starts where .heap ends and has no limit check, and
// the USB stack uses it. The predelay, read once a sample, is in SDRAM.
float __attribute__((section(".heap"))) sea_mem[StormEngine::kFastFloats];
float DSY_SDRAM_BSS predelay_mem[StormEngine::kSlowFloats];

uint8_t DSY_SDRAM_BSS web_buffer[weblink::kMaxMessage];

DaisyPod       hw;
MidiUsbHandler usb_midi;
StormEngine    engine;
Limiter        limiter; // on dry + wet, so a loud wet signal doesn't clip
CpuLoadMeter   cpu_meter;
QspiFlash      flash(hw.seed.qspi);
PresetsFor<kNumParams> presets;

constexpr int kNumKnobPages = 3;
constexpr int kPresetPage   = kNumKnobPages; // after the knob pages; its knobs do nothing
constexpr int kNumPages     = kNumKnobPages + 1;
constexpr int kNumKnobs     = 2;

// Raw knob positions (0..1) per page, initial values chosen to sound sensible.
PagedKnobs<kNumPages, kNumKnobs> pages({
    {0.42f, 0.35f}, // decay 4 s, mix
    {0.7f, 0.6f},   // size, tone
    {0.25f, 0.0f},  // depth, no tide
    {0.0f, 0.0f},   // presets: unused
});

// ---------------------------------------------------------------- State
//
// What the Pod and the web page set. Only the audio callback writes it;
// the main loop reads it for the LEDs and the page.

// Each page's encoder choice: width, predelay; the third page has none.
constexpr int kChoiceCounts[kNumKnobPages] = {StormEngine::kNumWidths, StormEngine::kNumPredelays, 0};
int  choices[kNumKnobPages] = {StormEngine::kWide, 1, 0};
bool active   = true;
bool freeze   = false;
bool trails   = true;
bool kill_dry = false;
bool mono_in  = false;

PressHold encoder_press(600); // hold: mono in, or on the preset page, save
PressHold button1(500);       // tap: on/off; hold: Calm, with the storm
PressHold button2(500);       // tap: Freeze; hold: Squall, with the storm

// Read by the main loop for the LEDs: flash until these times (ms).
volatile uint32_t choice_flash_until = 0;
volatile uint32_t mono_flash_until   = 0;

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
int                 trace_samples = 0, trace_period = 48;

volatile float peaks[3]    = {}; // in, wet, out
volatile bool  reset_peaks = false;

Telemetry DSY_SDRAM_BSS telemetry; // the main loop's copy

// LED 2 flashes the new choice if its page is showing.
void SetChoice(int page, int value, uint32_t now_ms)
{
    int n = kChoiceCounts[page];
    if(n == 0)
        return;
    choices[page] = value < 0 ? 0 : (value >= n ? n - 1 : value);
    if(page == pages.Page())
        choice_flash_until = now_ms + 1000;
}

// A value from the page, in the audio callback.
void SetParam(int index, int raw, uint32_t now_ms)
{
    switch(index)
    {
        case kParamWidth: SetChoice(0, raw, now_ms); break;
        case kParamPredelay: SetChoice(1, raw, now_ms); break;
        case kParamTrails: trails = raw != 0; break;
        case kParamKillDry: kill_dry = raw != 0; break;
        case kParamActive: active = raw != 0; break;
        case kParamFreeze: freeze = raw != 0; break;
        case kParamMono: mono_in = raw != 0; break;
        default:
            if(index < kNumKnobParams)
                pages.Set(index / 2, index % 2, raw / 16383.0f);
            break;
    }
}

// A value from a preset or the saved settings, through SetParam.
void SetPresetParam(int index, int raw)
{
    SetParam(index, raw, System::GetNow());
}

// A value for the page, in the main loop.
int ParamRaw(int index)
{
    switch(index)
    {
        case kParamWidth: return choices[0];
        case kParamPredelay: return choices[1];
        case kParamTrails: return trails;
        case kParamKillDry: return kill_dry;
        case kParamActive: return active;
        case kParamFreeze: return freeze;
        case kParamMono: return mono_in;
        default: return (int)(pages.Value(index / 2, index % 2) * 16383.0f + 0.5f);
    }
}

// Once per block, in the audio callback: a trace point every millisecond.
void Collect(size_t size)
{
    trace_samples += (int)size;
    if(trace_samples < trace_period)
        return;
    trace_samples = 0;

    StormEngine::Probe p     = engine.GetProbe();
    float             *point = trace_ring[trace_written & (kTraceRing - 1)];
    point[kTraceSea]         = 10.0f * log10f(p.level + 1e-7f);
    point[kTraceTide]        = p.tide;
    point[kTraceDecay]       = p.hold > 0.5f ? kHeldDecay : fminf(p.rt60, kHeldDecay);
    trace_written            = trace_written + 1; // after the point is in place
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

    weblink::Writer &w = web_out;
    switch(web.Next(now_ms, changed))
    {
        case weblink::Session::Send::kHello: WriteHello(w, hw.AudioSampleRate()); break;
        case weblink::Session::Send::kParam: web.WriteParams(w); break;
        case weblink::Session::Send::kState: web.WriteState(w, raw, web_state); break;
        case weblink::Session::Send::kFrame:
            ReadTelemetry();
            WriteFrame(w, telemetry, cpu_meter.GetAvgCpuLoad(), cpu_meter.GetMaxCpuLoad());
            presets.WriteSection(w);
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

void HandleCc(uint8_t cc, uint8_t value, uint32_t now_ms)
{
    bool on = value >= 64;
    switch(cc)
    {
        case 28: SetChoice(0, value * StormEngine::kNumWidths / 128, now_ms); break;
        case 29: SetChoice(1, value * StormEngine::kNumPredelays / 128, now_ms); break;
        case 64: // a sustain pedal holds the sea while it's down
        case 103: freeze = on; break;
        case 102: active = on; break;
        case 104: mono_in = on; break;
        case 107: trails = on; break;
        case 108: kill_dry = on; break;
        default:
            // 14-19: the knobs, page by page
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
        case ProgramChange: presets.Select(e.AsProgramChange().program); break;
        case SystemCommon:
            // The web page's settings and presets.
            if(e.sc_type == SystemExclusive)
            {
                weblink::Command c;
                if(web.Receive(e.sysex_data, e.sysex_message_len, now_ms, &c) && !presets.Handle(c))
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

    // The buttons act on a tap, when let go: a hold will be the storm's
    // (Calm and Squall).
    if(button1.Update(hw.button1.Pressed(), hw.button1.TimeHeldMs()) == PressHold::Event::kTap)
        active = !active;
    if(button2.Update(hw.button2.Pressed(), hw.button2.TimeHeldMs()) == PressHold::Event::kTap)
        freeze = !freeze;

    const DaisyPod::Knob knob_ids[kNumKnobs] = {DaisyPod::KNOB_1, DaisyPod::KNOB_2};
    float                knob[kNumKnobs];
    for(int k = 0; k < kNumKnobs; k++)
        knob[k] = hw.GetKnobValue(knob_ids[k]);

    // Long press toggles mono in (or saves, on the preset page) as soon as
    // the hold is long enough; a short press changes page on release.
    bool             on_presets = pages.Page() == kPresetPage;
    PressHold::Event enc        = encoder_press.Update(hw.encoder.Pressed(),
                                                hw.encoder.TimeHeldMs());
    if(enc == PressHold::Event::kHold && on_presets)
        presets.Save();
    else if(enc == PressHold::Event::kHold)
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
    if(inc != 0 && pages.Page() == kPresetPage)
        presets.Step(inc);
    else if(inc != 0)
        SetChoice(pages.Page(), choices[pages.Page()] + inc, now_ms);
    presets.Apply();

    StormEngine::Params p;
    p.decay    = pages.Value(0, 0);
    p.mix      = pages.Value(0, 1);
    p.size     = pages.Value(1, 0);
    p.tone     = pages.Value(1, 1);
    p.depth    = pages.Value(2, 0);
    p.tide     = pages.Value(2, 1);
    p.width    = choices[0];
    p.predelay = choices[1];
    p.active   = active;
    p.trails   = trails;
    p.freeze   = freeze;
    p.kill_dry = kill_dry;
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
    TuneSdramTimings();
    hw.SetAudioBlockSize(4);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    engine.Init(hw.AudioSampleRate(), hw.AudioBlockSize(), sea_mem, predelay_mem);
    limiter.Init(hw.AudioSampleRate());
    cpu_meter.Init(hw.AudioSampleRate(), hw.AudioBlockSize());
    web.Init(kParams, kNumParams);
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

    // The saved settings, then the last preset; or with both buttons held,
    // a factory reset.
    bool reset = FactoryResetHeld(hw);
    presets.Init(&flash, kRegionStormsea, kParams, kNumParams, ParamRaw, SetPresetParam, reset);

    // Every knob starts "not live" so the defaults above hold until a knob moves.
    hw.StartAdc();
    System::Delay(10); // let the ADC settle before taking the reference reading
    hw.ProcessAnalogControls();
    pages.Latch({hw.GetKnobValue(DaisyPod::KNOB_1), hw.GetKnobValue(DaisyPod::KNOB_2)});

    hw.StartAudio(AudioCallback);

    const float page_colours[kNumKnobPages][3] = {
        {0.0f, 0.0f, 1.0f}, // Sea: blue
        {0.0f, 1.0f, 1.0f}, // Shape: cyan
        {0.0f, 1.0f, 0.0f}, // Motion: green
    };
    const float width_colours[StormEngine::kNumWidths][3] = {
        {1.0f, 0.0f, 0.0f}, // narrow: red
        {0.0f, 1.0f, 0.0f}, // medium: green
        {0.0f, 0.0f, 1.0f}, // wide: blue
    };
    while(1)
    {
        uint32_t now  = System::GetNow();
        int      page = pages.Page();
        float    r, g, b;

        // LED 1: the mono in flash, else dark while off, pale moonlight
        // while frozen, and a calm deep blue otherwise.
        if(now < mono_flash_until)
        {
            r = 0.0f;
            g = b = mono_in ? 1.0f : 0.0f;
        }
        else if(!active)
            r = g = b = 0.0f;
        else if(freeze)
        {
            r = 0.7f;
            g = 0.8f;
            b = 1.0f;
        }
        else
        {
            r = 0.0f;
            g = 0.15f;
            b = 1.0f;
        }
        hw.led1.Set(r, g, b);

        // LED 2: the preset slot on the preset page, else the page colour,
        // or the page's choice just after a change.
        if(page == kPresetPage)
            presets.LedColour(now, &r, &g, &b);
        else if(now < choice_flash_until && page == 0)
        {
            r = width_colours[choices[0]][0];
            g = width_colours[choices[0]][1];
            b = width_colours[choices[0]][2];
        }
        else if(now < choice_flash_until && page == 1)
        {
            // The predelay: cyan, brighter the longer it is.
            float level = 0.1f + 0.9f * (float)choices[1] / (StormEngine::kNumPredelays - 1);
            r           = 0.0f;
            g = b = level;
        }
        else
        {
            r = page_colours[page][0];
            g = page_colours[page][1];
            b = page_colours[page][2];
        }
        hw.led2.Set(r, g, b);
        hw.UpdateLeds();

        presets.Poll(now);
        SendWeb(now);
        System::Delay(10);
    }
}

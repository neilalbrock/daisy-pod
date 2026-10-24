// Grainfall - granular and micro-loop effects for the Daisy Pod (Daisy Seed 3)
//
// Our own take on the Hologram Microcosm: 11 effects with 4 variations each,
// steered by macro controls, then pitch modulation, a resonant low-pass and
// the dry/wet mix. The two knobs are shared across five pages; press the
// encoder to change page (LED 2 shows which). After a page change a knob
// does nothing until it is moved, so values don't jump.
//
//   Page 1 (LED 2 blue)    Knob 1 - Activity: more or less of the effect
//                          Knob 2 - Repeats: how long the effect goes on
//   Page 2 (LED 2 magenta) Knob 1 - Shape: square, ramp up, triangle, ramp down
//                          Knob 2 - Time: subdivision 1/4, 1/2, TAP, 2x, 4x, 8x
//   Page 3 (LED 2 yellow)  Knob 1 - Filter: low-pass, fully open at max
//                          Knob 2 - Mix: dry to wet
//   Page 4 (LED 2 cyan)    Knob 1 - pitch mod depth
//                          Knob 2 - pitch mod rate
//   Page 5 (LED 2 white)   Knob 1 - filter resonance
//                          Knob 2 - effect volume (noon is unity, max x2)
//                          (a limiter holds dry + wet under -1 dBFS)
//   Encoder turn       - effect and variation, Mosaic A .. Warp D. LED 2
//                        flashes the effect's colour, LED 1 the variation
//                        (A red, B yellow, C green, D blue)
//   Encoder long press - reverse on/off
//   Button 1           - tap tempo (LED 1 blinks off on each beat)
//   Button 1 hold 1 s  - bypass (LED 1 red)
//   Button 2           - Hold sampler on/off; hold it for momentary
//   Both buttons 1 s   - mono in: the left input feeds both channels
//                        (LED 1 flashes cyan for on, dark for off)
//
// LED 1 is green when active, blue while Hold is on, cyan while reversed.
//
// MIDI on the Pod's TRS input and over USB, channel 1, follows the
// Microcosm's map: clock (MIDI Start follows it, Stop lets go), program
// change 1-44 for the effects, and CCs for the controls. See README.md.
//
// Over USB it also talks to the web control surface (web/, `make web`),
// streaming the buffer, voices, tempo, levels and CPU while the page is open.

#include <cmath>

#include "bypass_fade.h"
#include "colour.h"
#include "daisy_pod.h"
#include "grain_engine.h"
#include "limiter.h"
#include "paged_knobs.h"
#include "press_hold.h"
#include "tempo.h"
#include "util/CpuLoadMeter.h"
#include "web_link.h"
#include "web_view.h"

using namespace daisy;

constexpr size_t kBufferFrames = 1 << 20; // 21.8 s
constexpr size_t kHoldFrames   = 1 << 17; // 2.7 s: the longest hold plus its crossfade
float DSY_SDRAM_BSS buffer[kBufferFrames * 2];
float DSY_SDRAM_BSS hold_buffer[kHoldFrames * 2];
float DSY_SDRAM_BSS mod_buffer[PostFx::kModLen * 2]; // pitch mod delay

// For the web page: a peak per 256 frames (5 ms) of the buffer, and the
// message being sent.
constexpr size_t      kOverviewColumns = 4096;
uint8_t DSY_SDRAM_BSS overview[kOverviewColumns];
uint8_t DSY_SDRAM_BSS web_buffer[weblink::kMaxMessage];

DaisyPod       hw;
MidiUsbHandler usb_midi;
GrainEngine    engine;
Tempo          tempo;
Limiter        limiter; // on dry + wet, so a loud wet signal doesn't clip
BypassFade     bypass_fade;
CpuLoadMeter   cpu_meter;

constexpr int kNumPages = 5;
constexpr int kNumKnobs = 2;
constexpr int kNumSlots = GrainEngine::kNumEffects * GrainEngine::kNumVariations;

// Raw knob positions (0..1) per page, initial values chosen to sound sensible.
PagedKnobs<kNumPages, kNumKnobs> pages({
    {0.5f, 0.5f},  // activity, repeats
    {0.1f, 0.42f}, // shape square, time TAP
    {1.0f, 0.5f},  // filter open, mix 50 %
    {0.0f, 0.3f},  // mod depth off, rate
    {0.2f, 0.5f},  // resonance, volume unity
});

constexpr int      kMidiChannel = 0; // channel 1
constexpr uint32_t kMomentaryMs = 400; // button 2: held this long = momentary
constexpr uint32_t kChordMs     = 50;  // both buttons pressed this close = chord
constexpr uint32_t kChordHoldMs = 1000;

int  slot      = (int)GrainEngine::EffectId::kHaze * GrainEngine::kNumVariations;
bool bypass    = false;
bool hold_on   = false;
bool reverse   = false;
bool mono_in   = false;
uint32_t sample_count = 0; // running timestamp for MIDI and tap tempo

PressHold encoder_press(600);  // hold: reverse
PressHold button1_press(1000); // tap: tap tempo, hold: bypass

// Both-button chord for mono in. Button 2 waits kChordMs before acting, in
// case button 1 joins it.
bool     chord      = false; // both down together; ignore them until released
bool     chord_done = false; // this chord has toggled mono in
bool     b2_pending = false; // button 2 pressed, not yet acted on
uint32_t b1_down_ms = 0;
uint32_t b2_down_ms = 0;

// Read by the main loop for the LEDs.
volatile float    beat_phase       = 0.0f; // 0..1 through the current beat
volatile uint32_t effect_flash_til = 0;
volatile uint32_t mono_flash_til   = 0;

// Program change order on the Microcosm (Arp first), as encoder slot effects.
const GrainEngine::EffectId kPcEffects[GrainEngine::kNumEffects] = {
    GrainEngine::EffectId::kArp,
    GrainEngine::EffectId::kInterrupt,
    GrainEngine::EffectId::kBlocks,
    GrainEngine::EffectId::kGlide,
    GrainEngine::EffectId::kSeq,
    GrainEngine::EffectId::kMosaic,
    GrainEngine::EffectId::kHaze,
    GrainEngine::EffectId::kTunnel,
    GrainEngine::EffectId::kStrum,
    GrainEngine::EffectId::kPattern,
    GrainEngine::EffectId::kWarp,
};

void SelectSlot(int s, uint32_t now_ms)
{
    slot             = ((s % kNumSlots) + kNumSlots) % kNumSlots;
    effect_flash_til = now_ms + 1000;
}

void HandleCc(uint8_t cc, uint8_t value, uint32_t now)
{
    float v  = value / 127.0f;
    bool  on = value >= 64;
    switch(cc)
    {
        case 5: // subdivision, stepped 0-5
            pages.Set(1, 1, ((value < 5 ? value : 5) + 0.5f) / GrainEngine::kNumSubdivs);
            break;
        case 6: pages.Set(0, 0, v); break;  // activity
        case 7: pages.Set(1, 0, v); break;  // shape
        case 8: pages.Set(2, 0, v); break;  // filter
        case 9: pages.Set(2, 1, v); break;  // mix
        case 10: tempo.SetBpm(40.0f + v * 200.0f); break; // time, as tempo
        case 11: pages.Set(0, 1, v); break; // repeats
        case 14: pages.Set(3, 1, v); break; // mod rate
        case 15: pages.Set(4, 0, v); break; // resonance
        case 16: pages.Set(4, 1, v); break; // effect volume
        case 19: pages.Set(3, 0, v); break; // mod depth
        case 47: // reverse: toggles on press, so a button's release does nothing
            if(on)
                reverse = !reverse;
            break;
        case 48: hold_on = on; break;
        case 93:
            if(on)
                tempo.Tap(now);
            break;
        case 102: bypass = !on; break;
        default: break;
    }
}

// ---------------------------------------------------------------- Web link
//
// The page in web/ (see web_view.h). It sets values through SysEx, which
// arrives with the other MIDI in the audio callback; the main loop sends it
// what it needs.

using namespace web_view;

weblink::Session web;
weblink::Writer  web_out;
int              web_state[kNumParams]; // values in the last state sent
OverviewCursor   overview_cursor = {0};

// Filled by the audio callback when the main loop asks, so a frame shows
// one moment.
Telemetry DSY_SDRAM_BSS telemetry;
volatile bool           want_telemetry = false;
float                   peaks[3]       = {}; // running, in the callback

// A value from the page, in the audio callback.
void SetParam(int index, int raw, uint32_t now, uint32_t now_ms)
{
    float v = raw / 16383.0f;
    switch(index)
    {
        case kParamEffect: SelectSlot(raw, now_ms); break;
        case kParamShape: pages.Set(1, 0, (raw + 0.5f) / 4.0f); break;
        case kParamTime: pages.Set(1, 1, (raw + 0.5f) / GrainEngine::kNumSubdivs); break;
        case kParamTempo: tempo.SetBpm(kMinBpm + v * kBpmRange); break;
        case kParamTap: tempo.Tap(now); break;
        case kParamReverse: reverse = raw != 0; break;
        case kParamHold: hold_on = raw != 0; break;
        case kParamBypass: bypass = raw != 0; break;
        case kParamMono: mono_in = raw != 0; break;
        default:
            if(index >= kParamActivity && index <= kParamVolume)
                pages.Set((index - 1) / 2, (index - 1) % 2, v);
            break;
    }
}

// A value for the page, in the main loop.
int ParamRaw(int index)
{
    auto unit = [](float v) { return (int)(fminf(fmaxf(v, 0.0f), 1.0f) * 16383.0f + 0.5f); };
    switch(index)
    {
        case kParamEffect: return slot;
        case kParamShape: return (int)fminf(pages.Value(1, 0) * 4.0f, 3.0f);
        case kParamTime:
            return (int)fminf(pages.Value(1, 1) * GrainEngine::kNumSubdivs,
                              GrainEngine::kNumSubdivs - 1);
        case kParamTempo:
        {
            float bpm = 60.0f * hw.AudioSampleRate() / tempo.SamplesPerBeat();
            return unit((bpm - kMinBpm) / kBpmRange);
        }
        case kParamTap: return 0;
        case kParamReverse: return reverse;
        case kParamHold: return hold_on;
        case kParamBypass: return bypass;
        case kParamMono: return mono_in;
        default: return unit(pages.Value((index - 1) / 2, (index - 1) % 2));
    }
}

// In the audio callback, when asked.
void FillTelemetry(int subdiv)
{
    Snapshot(engine, &telemetry);
    for(int i = 0; i < 3; i++)
    {
        telemetry.peaks[i] = peaks[i];
        peaks[i]           = 0.0f;
    }
    telemetry.beat             = tempo.BeatPos();
    telemetry.samples_per_beat = tempo.SamplesPerBeat();
    telemetry.steps_per_beat   = GrainEngine::StepsPerBeat(subdiv);
    telemetry.following        = tempo.Following();
}

// Asks the audio callback for a snapshot and waits for it (a block, 83 us).
bool TakeTelemetry()
{
    want_telemetry = true;
    uint32_t start = System::GetUs();
    while(want_telemetry)
    {
        if(System::GetUs() - start > 2000)
        {
            want_telemetry = false;
            return false;
        }
    }
    return true;
}

// From the main loop: sends the page the next message it needs, if any.
void SendWeb(uint32_t now_ms)
{
    int  raw[kNumParams];
    bool changed = false;
    for(int i = 0; i < kNumParams; i++)
    {
        raw[i] = ParamRaw(i);
        // The tempo wavers with MIDI clock; the regular resend covers it.
        if(i != kParamTempo && raw[i] != web_state[i])
            changed = true;
    }

    weblink::Writer &w     = web_out;
    int              param = 0;
    switch(web.Next(now_ms, changed, &param))
    {
        case weblink::Session::Send::kHello:
            WriteHello(w, hw.AudioSampleRate(), kBufferFrames, kOverviewColumns);
            break;
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
            if(!TakeTelemetry())
                return;
            WriteFrame(w, telemetry, hw.AudioSampleRate(), cpu_meter.GetAvgCpuLoad(),
                       cpu_meter.GetMaxCpuLoad(), overview, kOverviewColumns,
                       web.TakeRestart(), &overview_cursor);
            cpu_meter.Reset();
            break;
        default: return;
    }
    size_t size = w.End();
    if(size > 0)
        usb_midi.SendMessage(w.Data(), size);
}

// ---------------------------------------------------------------- MIDI

void HandleMidi(MidiEvent e, uint32_t now, uint32_t now_ms)
{
    switch(e.type)
    {
        case SystemRealTime:
            switch(e.srt_type)
            {
                case TimingClock: tempo.MidiTick(now); break;
                case Start: tempo.MidiStart(); break;
                case Continue: tempo.MidiContinue(); break;
                case Stop: tempo.MidiStop(); break;
                default: break;
            }
            break;
        case ControlChange:
        {
            ControlChangeEvent cc = e.AsControlChange();
            if(cc.channel == kMidiChannel)
                HandleCc(cc.control_number, cc.value, now);
            break;
        }
        case SystemCommon:
            if(e.sc_type == SystemExclusive)
            {
                weblink::Command c;
                if(web.Receive(e.sysex_data, e.sysex_message_len, now_ms, &c))
                    SetParam(c.index, c.raw, now, now_ms);
            }
            break;
        case ProgramChange:
        {
            ProgramChangeEvent pc = e.AsProgramChange();
            if(pc.channel == kMidiChannel && pc.program < kNumSlots)
            {
                int effect = (int)kPcEffects[pc.program / GrainEngine::kNumVariations];
                SelectSlot(effect * GrainEngine::kNumVariations
                               + pc.program % GrainEngine::kNumVariations,
                           now_ms);
            }
            break;
        }
        default: break;
    }
}

void HandleButtons(uint32_t now, uint32_t now_ms)
{
    bool b1 = hw.button1.Pressed(), b2 = hw.button2.Pressed();

    // A chord is both buttons going down within kChordMs of each other.
    bool start_chord = false;
    if(hw.button1.RisingEdge())
    {
        b1_down_ms  = now_ms;
        start_chord = b2 && now_ms - b2_down_ms < kChordMs;
    }
    if(hw.button2.RisingEdge())
    {
        b2_down_ms  = now_ms;
        b2_pending  = true;
        start_chord = start_chord || (b1 && now_ms - b1_down_ms < kChordMs);
    }
    if(start_chord && !chord)
    {
        chord      = true;
        chord_done = false;
        b2_pending = false;
    }
    if(chord)
    {
        if(!chord_done && b1 && b2
           && hw.button1.TimeHeldMs() >= kChordHoldMs
           && hw.button2.TimeHeldMs() >= kChordHoldMs)
        {
            mono_in        = !mono_in;
            mono_flash_til = now_ms + 500;
            chord_done     = true;
        }
        button1_press.Update(b1, hw.button1.TimeHeldMs()); // keep it in step, ignore it
        if(!b1 && !b2)
            chord = false;
        return;
    }

    // Button 1: a tap is tap tempo, a 1 s hold toggles bypass.
    PressHold::Event e1 = button1_press.Update(b1, hw.button1.TimeHeldMs());
    if(e1 == PressHold::Event::kTap)
        tempo.Tap(now);
    else if(e1 == PressHold::Event::kHold)
        bypass = !bypass;

    // Button 2: toggles Hold; releasing after a long press turns it back off.
    if(b2_pending && (!b2 || now_ms - b2_down_ms >= kChordMs))
    {
        b2_pending = false;
        hold_on    = !hold_on;
    }
    if(hw.button2.FallingEdge() && now_ms - b2_down_ms >= kMomentaryMs)
        hold_on = false;
}

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    cpu_meter.OnBlockStart();
    hw.ProcessAllControls();
    uint32_t now    = sample_count;
    uint32_t now_ms = System::GetNow();

    // MIDI arrives in the background; polling once per 4-sample block keeps
    // the clock timestamps within a block of the truth.
    hw.midi.Listen();
    usb_midi.Listen();
    while(hw.midi.HasEvents())
        HandleMidi(hw.midi.PopEvent(), now, now_ms);
    while(usb_midi.HasEvents())
        HandleMidi(usb_midi.PopEvent(), now, now_ms);

    HandleButtons(now, now_ms);

    int inc = hw.encoder.Increment();
    if(inc != 0)
        SelectSlot(slot + inc, now_ms);

    const DaisyPod::Knob knob_ids[kNumKnobs] = {DaisyPod::KNOB_1, DaisyPod::KNOB_2};
    float                knob[kNumKnobs];
    for(int k = 0; k < kNumKnobs; k++)
        knob[k] = hw.GetKnobValue(knob_ids[k]);

    // Long press toggles reverse as soon as the hold is long enough; a short
    // press changes page on release.
    PressHold::Event enc = encoder_press.Update(hw.encoder.Pressed(),
                                                hw.encoder.TimeHeldMs());
    if(enc == PressHold::Event::kHold)
        reverse = !reverse;
    else if(enc == PressHold::Event::kTap)
        pages.NextPage(knob);
    pages.Update(knob);

    tempo.Update(now);
    beat_phase = tempo.BeatPos() - floorf(tempo.BeatPos());

    GrainEngine::Params p;
    p.effect           = (GrainEngine::EffectId)(slot / GrainEngine::kNumVariations);
    p.variation        = slot % GrainEngine::kNumVariations;
    p.activity         = pages.Value(0, 0);
    p.repeats          = pages.Value(0, 1);
    p.shape            = (int)fminf(pages.Value(1, 0) * 4.0f, 3.0f);
    p.subdiv           = (int)fminf(pages.Value(1, 1) * GrainEngine::kNumSubdivs,
                          GrainEngine::kNumSubdivs - 1);
    p.reverse          = reverse;
    p.hold             = hold_on;
    p.samples_per_beat = tempo.SamplesPerBeat();
    p.beat_pos         = tempo.BeatPos();
    p.post.filter      = pages.Value(2, 0);
    p.post.mod_depth   = pages.Value(3, 0);
    p.post.mod_rate    = pages.Value(3, 1);
    p.post.resonance   = pages.Value(4, 0);
    p.post.volume      = pages.Value(4, 1);
    engine.BeginBlock(p, (int)size);

    // Equal-power mix: the dry and wet are mostly uncorrelated.
    float mix      = pages.Value(2, 1);
    float dry_gain = cosf(mix * 1.57079633f);
    float wet_gain = sinf(mix * 1.57079633f);

    for(size_t i = 0; i < size; i++)
    {
        float in_l = in[0][i];
        float in_r = mono_in ? in_l : in[1][i];

        // Keep the engine running in bypass so the buffer stays current.
        float wet_l, wet_r;
        engine.Process(in_l, in_r, &wet_l, &wet_r);

        float fx_l = in_l * dry_gain + wet_l * wet_gain;
        float fx_r = in_r * dry_gain + wet_r * wet_gain;
        limiter.Process(&fx_l, &fx_r);
        bypass_fade.Process(bypass, in_l, in_r, &fx_l, &fx_r);
        out[0][i] = fx_l;
        out[1][i] = fx_r;

        peaks[0] = fmaxf(peaks[0], fmaxf(fabsf(in_l), fabsf(in_r)));
        peaks[1] = fmaxf(peaks[1], fmaxf(fabsf(wet_l), fabsf(wet_r)));
        peaks[2] = fmaxf(peaks[2], fmaxf(fabsf(fx_l), fabsf(fx_r)));
    }
    sample_count += size;

    if(want_telemetry)
    {
        FillTelemetry(p.subdiv);
        want_telemetry = false;
    }
    cpu_meter.OnBlockEnd();
}

int main(void)
{
    hw.Init(true); // 480 MHz: the delay taps need the headroom
    hw.SetAudioBlockSize(4);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    engine.Init(hw.AudioSampleRate(), buffer, kBufferFrames, hold_buffer,
                kHoldFrames, mod_buffer);
    engine.SetOverview(overview, kOverviewColumns);
    cpu_meter.Init(hw.AudioSampleRate(), hw.AudioBlockSize());
    web.Init(kNumParams);
    web_out.Init(web_buffer);
    tempo.Init(hw.AudioSampleRate());
    limiter.Init(hw.AudioSampleRate());
    bypass_fade.Init(hw.AudioSampleRate());

    MidiUsbHandler::Config usb_cfg;
    usb_cfg.transport_config.periph = MidiUsbTransport::Config::INTERNAL;
    // A long SysEx message to the web page can still be going out when the
    // next one is ready; wait up to 2 ms for it (in the main loop).
    usb_cfg.transport_config.tx_retry_count = 20;
    usb_midi.Init(usb_cfg);
    usb_midi.StartReceive();
    hw.midi.StartReceive();

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
        {1.0f, 1.0f, 1.0f}, // white
    };
    const float variation_colours[GrainEngine::kNumVariations][3] = {
        {1.0f, 0.0f, 0.0f}, // A red
        {1.0f, 0.7f, 0.0f}, // B yellow
        {0.0f, 1.0f, 0.0f}, // C green
        {0.0f, 0.0f, 1.0f}, // D blue
    };
    while(1)
    {
        uint32_t now    = System::GetNow();
        int      effect = slot / GrainEngine::kNumVariations;

        // LED 1: mono in / variation flashes, then bypass, Hold, reverse or
        // active, dark for the start of each beat.
        if(now < mono_flash_til)
        {
            float c = mono_in ? 1.0f : 0.0f;
            hw.led1.Set(0.0f, c, c);
        }
        else if(now < effect_flash_til)
        {
            const float *c = variation_colours[slot % GrainEngine::kNumVariations];
            hw.led1.Set(c[0], c[1], c[2]);
        }
        else if(bypass)
            hw.led1.Set(1.0f, 0.0f, 0.0f);
        else if(beat_phase < 0.1f)
            hw.led1.Set(0.0f, 0.0f, 0.0f);
        else if(hold_on)
            hw.led1.Set(0.0f, 0.0f, 1.0f);
        else if(reverse)
            hw.led1.Set(0.0f, 1.0f, 1.0f);
        else
            hw.led1.Set(0.0f, 1.0f, 0.0f);

        // LED 2: page colour, or the effect's colour just after a change.
        if(now < effect_flash_til)
        {
            float r, g, b;
            Hue(0.9f * effect / (GrainEngine::kNumEffects - 1), &r, &g, &b);
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

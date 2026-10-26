// Driftnet - a two-channel micro-looper and spatial effect for the Daisy
// Pod (Daisy Seed 3)
//
// Our own take on Chase Bliss's MOOD MKII. The Wet Channel is a real-time
// effect; the Micro-Looper is always listening and loops whatever you just
// played. The two feed each other, and the CLOCK sets the sample rate they
// both run at: lower it for longer loops and delays, lower pitch and more
// grit. The two knobs are shared across three pages, one per column of the
// pedal; press the encoder to change page (LED 2 shows which). After a page
// change a knob does nothing until it is moved, so values don't jump.
//
//   Page 1 (LED 2 blue)    Wet Channel, by mode:
//                            Reverb: Knob 1 - TIME: size and decay
//                                    Knob 2 - MODIFY: smear, multi-tap to wash
//                            Delay:  Knob 1 - TIME: delay time
//                                    Knob 2 - MODIFY: feedback (holds at max)
//                            Slip:   Knob 1 - TIME: sampling window
//                                    Knob 2 - MODIFY: speed in semitones,
//                                             2x reversed .. 1x at noon .. 2x
//   Page 2 (LED 2 magenta) Micro-Looper, by mode:
//                            Env:     Knob 1 - LENGTH: slice size
//                                     Knob 2 - MODIFY: input sensitivity
//                            Tape:    Knob 1 - LENGTH: shortens the loop, to 1/32
//                                     Knob 2 - MODIFY: speed, 4x reverse .. 4x forward
//                            Stretch: Knob 1 - LENGTH: grain size
//                                     Knob 2 - MODIFY: stretch, frozen at noon,
//                                              none at either end (reversed at min)
//   Page 3 (LED 2 yellow)  Knob 1 - CLOCK: 2k .. 64k in steps of fifths and fourths
//                          Knob 2 - MIX: dry and wet both full at noon
//                          (a limiter holds dry + wet under -1 dBFS)
//   Encoder turn - the page's three-way toggle; LED 2 flashes red, green or
//                  blue for left, middle or right:
//                    page 1: Reverb, Delay, Slip
//                    page 2: Env, Tape, Stretch
//                    page 3: routing, what the Wet Channel hears while the
//                            loop plays: input, input + loop, loop only
//   Encoder long press - mono in (MISO): the left input feeds both channels
//                        (LED 1 flashes cyan for on, dark for off)
//   Button 1           - Wet Channel on; tap again for off
//   Button 1 hold      - freeze, while held (LATCH: until held again)
//   Button 2           - Micro-Looper: play the last moments; tap again to
//                        go back to listening (recording over the loop)
//   Button 2 hold      - overdub, while held (LATCH: until held again)
//
// LED 1 is green while the Wet Channel is on and white while frozen, with
// red added while the loop plays. It blinks off at the start of each pass,
// and its red flickers while overdubbing. It pulses cyan in Synth Mode.
//
// Everything else - MOOD's hidden options and DIP switches, ramping, tempo
// sync and Synth Mode - is over MIDI on channel 2 (TRS and USB) with MOOD's
// CC map, or from the web page (web/, `make web`). See README.md. Nothing is
// saved: it all starts from the defaults at power-up.

#include "daisy_pod.h"
#include "bypass_fade.h"
#include "drift_engine.h"
#include "limiter.h"
#include "paged_knobs.h"
#include "press_hold.h"
#include "ramp.h"
#include "synth.h"
#include "tempo.h"
#include "util/CpuLoadMeter.h"
#include "web_view.h"

using namespace daisy;
using namespace web_view;

constexpr size_t kLoopFrames   = 1 << 17; // 2.7 s at 48k, 65 s at 2k
constexpr size_t kDelayFrames  = 1 << 17;
constexpr size_t kSlipFrames   = 1 << 19; // over three of Slip's longest windows
constexpr size_t kReverbFrames = 28672;   // 224 KB, then 13 KB of diffusers
float DSY_SDRAM_BSS loop_buffer[kLoopFrames * 2];
float DSY_SDRAM_BSS delay_buffer[kDelayFrames * 2];
float DSY_SDRAM_BSS slip_buffer[kSlipFrames * 2];

// The Reverb reads 16 taps a tick, so its line goes in the 256 KB of RAM_D2,
// which reads far faster than SDRAM. BOOT_SRAM apps leave RAM_D2 to the
// heap, so the linker script's .heap section is the way in. Leave room:
// malloc's heap starts where .heap ends and has no limit check, and printf
// uses it.
float __attribute__((section(".heap")))
reverb_buffer[kReverbFrames * 2 + WetReverb::kDiffuserFloats]; // and its diffusers

// For the web page: a peak per column of the loop's ring, and the message
// being sent.
uint8_t DSY_SDRAM_BSS overview[kOverviewColumns];
uint8_t DSY_SDRAM_BSS web_buffer[weblink::kMaxMessage];

DaisyPod       hw;
MidiUsbHandler usb_midi;
DriftEngine    engine;
Tempo          tempo;
Ramp           ramp;
Synth          synth;
Limiter        limiter; // on dry + wet, so a loud wet signal doesn't clip
BypassFade     bypass_fade;
CpuLoadMeter   cpu_meter;

constexpr int kNumPages = 3;
constexpr int kNumKnobs = 2;

// Raw knob positions (0..1) per page, initial values chosen to sound sensible.
PagedKnobs<kNumPages, kNumKnobs> pages({
    {0.5f, 0.4f},  // time, feedback
    {1.0f, 0.72f}, // full length, 1x forward
    {0.9f, 0.5f},  // clock 48k, mix both full
});

constexpr int      kMidiChannel = 1;   // channel 2, MOOD's default
constexpr uint32_t kHoldMs      = 400; // a button held this long freezes or overdubs

// ---------------------------------------------------------------- State
//
// What the Pod, MIDI and the web page all set. Only the audio callback
// writes it; the main loop reads it for the LEDs and the page.

// Each page's three-way toggle, 0..2: wet mode, loop mode, routing.
int  toggles[kNumPages] = {1, 1, 0}; // Delay, Tape, input only
bool wet_on             = false;
bool freeze_on = false, freeze_held = false;   // set (CC, page, LATCH), and the button
bool overdub_on = false, overdub_held = false; // the same for overdubbing

// Hidden options
float tone = 1.0f, width = 1.0f, direct = 0.0f, fade = 1.0f, balance = 0.5f;
int   sync_mode = 1, solo = 1, loop_length = 1; // Off, Both, Full

// DIP switches
bool classic = false, mono_in = false, spread = false, dry_kill = false;
bool trails = false, latch = false, no_dub = false, smooth = false;

Ramp::Settings  ramp_settings;
Synth::Settings synth_settings;
bool            follow_clock = true;

PressHold encoder_press(600); // hold: mono in
PressHold button1_press(kHoldMs);
PressHold button2_press(kHoldMs);
bool      b1_switched_on = false; // this press turned its channel on, so
bool      b2_switched_on = false; // its release mustn't turn it off again

uint32_t sample_count = 0; // running timestamp for MIDI clock and tap tempo

// Synth Mode ends when the CLOCK knob moves away from where it was.
bool  synth_was_active = false;
float synth_clock_ref  = 0.0f;

// Read by the main loop for the LEDs: flash until these times (ms).
volatile uint32_t toggle_flash_until = 0;
volatile uint32_t mono_flash_until   = 0;

bool Freeze()
{
    return freeze_on || freeze_held;
}

void SetWetOn(bool on)
{
    if(on && !wet_on)
        ramp.Trigger();
    wet_on = on;
}

void Engage()
{
    if(!engine.LooperPlaying())
    {
        engine.LooperEngage();
        ramp.Trigger();
    }
}

int ThreeWay(uint8_t value) // MOOD's toggles: 0-1 left, 2 middle, above right
{
    return value <= 1 ? 0 : (value == 2 ? 1 : 2);
}

// ---------------------------------------------------------------- MIDI

void HandleCc(uint8_t cc, uint8_t value, uint32_t now)
{
    float v  = value / 127.0f;
    bool  on = value >= 1;
    switch(cc)
    {
        case 1: synth.ModWheel(v); break;
        case 14: pages.Set(0, 0, v); break; // TIME
        case 15: pages.Set(2, 1, v); break; // MIX
        case 16: pages.Set(1, 0, v); break; // LENGTH
        case 17: pages.Set(0, 1, v); break; // wet MODIFY
        case 18: // CLOCK: stepped by MOOD's table, unless SMOOTH
            pages.Set(2, 0, smooth ? v : ClockDomain::StepForMidi(value) / 10.0f);
            break;
        case 19: pages.Set(1, 1, v); break; // loop MODIFY
        case 20: ramp_settings.speed = v; break;
        case 21: toggles[0] = ThreeWay(value); break;
        case 22: toggles[2] = ThreeWay(value); break;
        case 23: toggles[1] = ThreeWay(value); break;
        case 24: width = v; break;
        case 25: ramp_settings.shape = v; break;
        case 26: fade = v; break;
        case 27: tone = v; break;
        case 28: balance = v; break;
        case 29: direct = v; break;
        case 31: sync_mode = ThreeWay(value); break;
        case 32: solo = ThreeWay(value); break;
        case 33: loop_length = value <= 1 ? 0 : 1; break;
        case 51: follow_clock = on; break;
        case 52: ramp_settings.running = on; break;
        case 57: synth_settings.octave = value > 9 ? 9 : value; break;
        case 58:
            synth_settings.output = value == 0   ? Synth::Output::kOpen
                                    : value == 1 ? Synth::Output::kOnOff
                                                 : Synth::Output::kAdsr;
            break;
        case 59: synth.Exit(); break;
        case 61: ramp_settings.targets[Ramp::kTime] = on; break;
        case 62: ramp_settings.targets[Ramp::kWetModify] = on; break;
        case 63: ramp_settings.targets[Ramp::kClock] = on; break;
        case 64: ramp_settings.targets[Ramp::kLoopModify] = on; break;
        case 65: ramp_settings.targets[Ramp::kLength] = on; break;
        case 66: ramp_settings.bounce = on; break;
        case 67: ramp_settings.top = on; break;
        case 68: ramp_settings.reverse = on; break;
        case 71: classic = on; break;
        case 72: mono_in = on; break;
        case 73: spread = on; break;
        case 74: dry_kill = on; break;
        case 75: trails = on; break;
        case 76: latch = on; break;
        case 77: no_dub = on; break;
        case 78: smooth = on; break;
        case 80: synth_settings.attack = v; break;
        case 81: synth_settings.decay = v; break;
        case 82: synth_settings.sustain = v; break;
        case 83: synth_settings.release = v; break;
        case 84: synth_settings.portamento = v; break;
        case 93:
        case 107:
            if(on)
                tempo.Tap(now);
            break;
        case 100:
            ramp_settings.pedal      = v;
            ramp_settings.expression = true;
            break;
        case 102: SetWetOn(on); break;
        case 103:
            if(on)
                Engage();
            else
                engine.LooperRelease();
            break;
        case 105: freeze_on = on; break;
        case 106: overdub_on = on; break;
        case 110: // MIDI reset
            follow_clock               = true;
            synth_settings.octave      = 3;
            synth_settings.portamento  = 0.0f;
            synth_settings.output      = Synth::Output::kOpen;
            break;
        default: break;
    }
}

// ---------------------------------------------------------------- Web link
//
// The page in web/ (see web_view.h). It sets values through SysEx, which
// arrives with the other MIDI in the audio callback; the main loop sends it
// what it needs.

weblink::Session web;
weblink::Writer  web_out;
int              web_state[kNumParams]; // values in the last state sent
OverviewCursor   overview_cursor = {0};

// Filled by the audio callback when the main loop asks, so a frame shows
// one moment.
Telemetry DSY_SDRAM_BSS telemetry;
volatile bool           want_telemetry = false;
float                   peaks[4]       = {}; // in, wet, loop, out, in the callback
float                   trace[kTracePoints][kNumTraces];
int                     trace_points = 0;
uint32_t                trace_next   = 0; // sample_count of the next point

// A value from the page, in the audio callback.
void SetParam(int index, int raw, uint32_t now)
{
    float v  = raw / 16383.0f;
    bool  on = raw != 0;
    bool *ramp_target = nullptr;
    switch(index)
    {
        case kParamWetMode: toggles[0] = raw; break;
        case kParamLoopMode: toggles[1] = raw; break;
        case kParamRouting: toggles[2] = raw; break;
        case kParamWetOn: SetWetOn(on); break;
        case kParamFreeze: freeze_on = on; break;
        case kParamLooper:
            if(on)
                Engage();
            else
                engine.LooperRelease();
            break;
        case kParamOverdub: overdub_on = on; break;
        case kParamTone: tone = v; break;
        case kParamWidth: width = v; break;
        case kParamDirect: direct = v; break;
        case kParamFade: fade = v; break;
        case kParamBalance: balance = v; break;
        case kParamSync: sync_mode = raw; break;
        case kParamSolo: solo = raw; break;
        case kParamLoopLength: loop_length = raw; break;
        case kParamClassic: classic = on; break;
        case kParamMiso: mono_in = on; break;
        case kParamSpread: spread = on; break;
        case kParamDryKill: dry_kill = on; break;
        case kParamTrails: trails = on; break;
        case kParamLatch: latch = on; break;
        case kParamNoDub: no_dub = on; break;
        case kParamSmooth: smooth = on; break;
        case kParamRampTime: ramp_target = &ramp_settings.targets[Ramp::kTime]; break;
        case kParamRampWetModify: ramp_target = &ramp_settings.targets[Ramp::kWetModify]; break;
        case kParamRampClock: ramp_target = &ramp_settings.targets[Ramp::kClock]; break;
        case kParamRampLoopModify: ramp_target = &ramp_settings.targets[Ramp::kLoopModify]; break;
        case kParamRampLength: ramp_target = &ramp_settings.targets[Ramp::kLength]; break;
        case kParamBounce: ramp_settings.bounce = on; break;
        case kParamSweep: ramp_settings.top = on; break;
        case kParamPolarity: ramp_settings.reverse = on; break;
        case kParamRampSpeed: ramp_settings.speed = v; break;
        case kParamWaveform: ramp_settings.shape = v; break;
        case kParamRamping: ramp_settings.running = on; break;
        case kParamExpression:
            ramp_settings.pedal      = v;
            ramp_settings.expression = true;
            break;
        case kParamExpressionOn: ramp_settings.expression = on; break;
        case kParamTap: tempo.Tap(now); break;
        case kParamTempo: tempo.SetBpm(kMinBpm + v * kBpmRange); break;
        case kParamFollowClock: follow_clock = on; break;
        case kParamSynced:
            if(on)
                tempo.SetBpm(60.0f * hw.AudioSampleRate() / tempo.SamplesPerBeat());
            else
            {
                tempo.ClearTempo();
                tempo.MidiStop();
            }
            break;
        case kParamSynth:
            if(on)
                synth.NoteOn(synth.Note(), 127);
            else
                synth.Exit();
            break;
        case kParamOutput: synth_settings.output = (Synth::Output)raw; break;
        case kParamOctave: synth_settings.octave = raw; break;
        case kParamAttack: synth_settings.attack = v; break;
        case kParamDecay: synth_settings.decay = v; break;
        case kParamSustain: synth_settings.sustain = v; break;
        case kParamRelease: synth_settings.release = v; break;
        case kParamPortamento: synth_settings.portamento = v; break;
        default:
            if(index >= kParamTime && index <= kParamMix)
                pages.Set(index / 2, index % 2, v);
            break;
    }
    if(ramp_target != nullptr)
        *ramp_target = on;
}

// A value for the page, in the main loop.
int ParamRaw(int index)
{
    auto unit = [](float v) { return (int)(fminf(fmaxf(v, 0.0f), 1.0f) * 16383.0f + 0.5f); };
    switch(index)
    {
        case kParamWetMode: return toggles[0];
        case kParamLoopMode: return toggles[1];
        case kParamRouting: return toggles[2];
        case kParamWetOn: return wet_on;
        case kParamFreeze: return Freeze();
        case kParamLooper: return engine.LooperPlaying();
        case kParamOverdub: return engine.Overdubbing();
        case kParamTone: return unit(tone);
        case kParamWidth: return unit(width);
        case kParamDirect: return unit(direct);
        case kParamFade: return unit(fade);
        case kParamBalance: return unit(balance);
        case kParamSync: return sync_mode;
        case kParamSolo: return solo;
        case kParamLoopLength: return loop_length;
        case kParamClassic: return classic;
        case kParamMiso: return mono_in;
        case kParamSpread: return spread;
        case kParamDryKill: return dry_kill;
        case kParamTrails: return trails;
        case kParamLatch: return latch;
        case kParamNoDub: return no_dub;
        case kParamSmooth: return smooth;
        case kParamRampTime: return ramp_settings.targets[Ramp::kTime];
        case kParamRampWetModify: return ramp_settings.targets[Ramp::kWetModify];
        case kParamRampClock: return ramp_settings.targets[Ramp::kClock];
        case kParamRampLoopModify: return ramp_settings.targets[Ramp::kLoopModify];
        case kParamRampLength: return ramp_settings.targets[Ramp::kLength];
        case kParamBounce: return ramp_settings.bounce;
        case kParamSweep: return ramp_settings.top;
        case kParamPolarity: return ramp_settings.reverse;
        case kParamRampSpeed: return unit(ramp_settings.speed);
        case kParamWaveform: return unit(ramp_settings.shape);
        case kParamRamping: return ramp_settings.running;
        case kParamExpression: return unit(ramp_settings.pedal);
        case kParamExpressionOn: return ramp_settings.expression;
        case kParamTap: return 0;
        case kParamTempo:
        {
            float bpm = 60.0f * hw.AudioSampleRate() / tempo.SamplesPerBeat();
            return unit((bpm - kMinBpm) / kBpmRange);
        }
        case kParamFollowClock: return follow_clock;
        case kParamSynced: return tempo.Following() || tempo.HasTempo();
        case kParamSynth: return synth.Active();
        case kParamOutput: return (int)synth_settings.output;
        case kParamOctave: return synth_settings.octave;
        case kParamAttack: return unit(synth_settings.attack);
        case kParamDecay: return unit(synth_settings.decay);
        case kParamSustain: return unit(synth_settings.sustain);
        case kParamRelease: return unit(synth_settings.release);
        case kParamPortamento: return unit(synth_settings.portamento);
        default: return unit(pages.Value(index / 2, index % 2));
    }
}

// In the audio callback, once a block: a trace point every kTracePeriodMs.
void CollectTrace()
{
    if((int32_t)(sample_count - trace_next) < 0)
        return;
    trace_next = sample_count + (uint32_t)(hw.AudioSampleRate() * kTracePeriodMs / 1000);
    if(trace_points == kTracePoints) // the page isn't reading: keep the newest
    {
        for(int i = 1; i < kTracePoints; i++)
            for(int k = 0; k < kNumTraces; k++)
                trace[i - 1][k] = trace[i][k];
        trace_points--;
    }
    float *t            = trace[trace_points++];
    t[kTraceClock]      = engine.ClockRate();
    t[kTraceInput]      = engine.Envelope();
    t[kTraceThreshold]  = engine.Threshold();
    t[kTraceRamp]       = ramp.Active() || ramp_settings.expression ? ramp.Position() : 0.0f;
}

// In the audio callback, when asked.
void FillTelemetry()
{
    Telemetry &t = telemetry;
    for(int i = 0; i < 4; i++)
    {
        t.peaks[i] = peaks[i];
        peaks[i]   = 0.0f;
    }
    engine.Snapshot(&t.loop);
    t.clock_hz         = engine.ClockRate();
    t.following        = tempo.Following();
    t.synced           = t.following || tempo.HasTempo();
    t.beat             = tempo.BeatPos();
    t.samples_per_beat = tempo.SamplesPerBeat();
    for(int i = 0; i < trace_points; i++)
        for(int k = 0; k < kNumTraces; k++)
            t.trace[i][k] = trace[i][k];
    t.num_points = trace_points;
    trace_points = 0;
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

// From the main loop: a slice of the overview, read from the ring (a peak
// of every 4th frame is plenty to draw).
void RefreshOverview(uint32_t *next)
{
    constexpr uint32_t kFramesPerColumn = kLoopFrames / kOverviewColumns;
    constexpr uint32_t kColumnsPerCall  = 40;
    for(uint32_t c = 0; c < kColumnsPerCall; c++)
    {
        uint32_t     col  = (*next + c) % kOverviewColumns;
        const float *f    = loop_buffer + 2 * col * kFramesPerColumn;
        float        peak = 0.0f;
        for(uint32_t i = 0; i < kFramesPerColumn; i += 4)
            peak = fmaxf(peak, fmaxf(fabsf(f[2 * i]), fabsf(f[2 * i + 1])));
        overview[col] = (uint8_t)(sqrtf(fminf(peak, 1.0f)) * 127.0f);
    }
    *next = (*next + kColumnsPerCall) % kOverviewColumns;
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
            WriteHello(w, hw.AudioSampleRate(), kLoopFrames);
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
            WriteFrame(w, telemetry, hw.AudioSampleRate(), kLoopFrames,
                       cpu_meter.GetAvgCpuLoad(), cpu_meter.GetMaxCpuLoad(), overview,
                       &overview_cursor);
            cpu_meter.Reset();
            break;
        default: return;
    }
    size_t size = w.End();
    if(size > 0)
        usb_midi.SendMessage(w.Data(), size);
}

void HandleMidi(MidiEvent e, uint32_t now, uint32_t now_ms)
{
    switch(e.type)
    {
        case SystemRealTime:
            // Synth Mode ignores the clock, as on MOOD.
            if(!follow_clock || synth.Active())
                break;
            switch(e.srt_type)
            {
                case TimingClock: tempo.MidiTick(now); break;
                case Start: tempo.MidiStart(); break;
                case Continue: tempo.MidiContinue(); break;
                case Stop: tempo.MidiStop(); break;
                default: break;
            }
            break;
        case NoteOn:
        {
            NoteOnEvent n = e.AsNoteOn();
            if(n.channel == kMidiChannel)
                synth.NoteOn(n.note, n.velocity);
            break;
        }
        case NoteOff:
        {
            NoteOffEvent n = e.AsNoteOff();
            if(n.channel == kMidiChannel)
                synth.NoteOff(n.note);
            break;
        }
        case PitchBend:
        {
            PitchBendEvent b = e.AsPitchBend();
            if(b.channel == kMidiChannel)
                synth.PitchBend(b.value + 8192);
            break;
        }
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
                    SetParam(c.index, c.raw, now);
            }
            break;
        default: break;
    }
}

// ---------------------------------------------------------------- Pod

void HandleButtons()
{
    // Button 1: a press switches the Wet Channel on at once; a tap switches
    // it off; holding freezes it until release (or, with LATCH, toggles it).
    if(hw.button1.RisingEdge())
    {
        b1_switched_on = !wet_on;
        SetWetOn(true);
    }
    PressHold::Event e1 = button1_press.Update(hw.button1.Pressed(), hw.button1.TimeHeldMs());
    if(e1 == PressHold::Event::kTap && !b1_switched_on)
        SetWetOn(false);
    if(latch)
    {
        if(e1 == PressHold::Event::kHold)
            freeze_on = !freeze_on;
        freeze_held = false;
    }
    else
        freeze_held = hw.button1.Pressed() && hw.button1.TimeHeldMs() >= kHoldMs;

    // Button 2: a press starts the loop at once, so it ends where you
    // pressed; a tap goes back to listening; holding overdubs.
    if(hw.button2.RisingEdge())
    {
        b2_switched_on = !engine.LooperPlaying();
        Engage();
    }
    PressHold::Event e2 = button2_press.Update(hw.button2.Pressed(), hw.button2.TimeHeldMs());
    if(e2 == PressHold::Event::kTap && !b2_switched_on)
        engine.LooperRelease();
    if(latch)
    {
        if(e2 == PressHold::Event::kHold)
            overdub_on = !overdub_on;
        overdub_held = false;
    }
    else
        overdub_held = hw.button2.Pressed() && hw.button2.TimeHeldMs() >= kHoldMs;
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

    HandleButtons();

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
        toggle_flash_until = 0;
    }
    pages.Update(knob);

    int inc = hw.encoder.Increment();
    if(inc != 0)
    {
        int &t             = toggles[pages.Page()];
        t                  = t + inc < 0 ? 0 : (t + inc > 2 ? 2 : t + inc);
        toggle_flash_until = now_ms + 1000;
    }

    // Tempo: MIDI clock (unless ignored) or taps.
    if(!follow_clock)
        tempo.MidiStop();
    tempo.Update(now);
    bool synced = tempo.Following() || tempo.HasTempo();

    // Synth Mode, which ends when the CLOCK moves.
    float clock = pages.Value(2, 0);
    if(synth.Active())
    {
        if(!synth_was_active)
            synth_clock_ref = clock;
        else if(fabsf(clock - synth_clock_ref) > 0.02f)
            synth.Exit();
    }
    synth_was_active = synth.Active();
    synth.Update(synth_settings);

    ramp.Update(ramp_settings);

    DriftEngine::Params p;
    p.time             = ramp.Apply(Ramp::kTime, pages.Value(0, 0));
    p.wet_modify       = ramp.Apply(Ramp::kWetModify, pages.Value(0, 1));
    p.length           = ramp.Apply(Ramp::kLength, pages.Value(1, 0));
    p.loop_modify      = ramp.Apply(Ramp::kLoopModify, pages.Value(1, 1));
    p.clock            = ramp.Apply(Ramp::kClock, clock);
    p.mix              = ramp.ApplyMix(pages.Value(2, 1));
    p.wet_mode         = (DriftEngine::WetMode)toggles[0];
    p.loop_mode        = (DriftEngine::LoopMode)toggles[1];
    p.routing          = (DriftEngine::Routing)toggles[2];
    p.wet_on           = wet_on;
    p.freeze           = Freeze();
    p.tone             = tone;
    p.width            = width;
    p.direct           = direct;
    p.fade             = fade;
    p.balance          = balance;
    p.sync             = (DriftEngine::Sync)sync_mode;
    p.solo             = (DriftEngine::Solo)solo;
    p.half_length      = loop_length == 0;
    p.classic          = classic;
    p.spread           = spread;
    p.dry_kill         = dry_kill;
    p.trails           = trails;
    p.no_dub           = no_dub;
    p.smooth           = smooth;
    p.samples_per_beat = synced ? tempo.SamplesPerBeat() : 0.0f;
    p.clock_hz         = synth.Active() ? synth.ClockHz() : 0.0f;
    p.gate             = synth.Active() ? synth.Gain() : 1.0f;
    engine.SetOverdub(overdub_on || overdub_held);
    engine.SetParams(p);

    bool bypassed = engine.Bypassed();
    for(size_t i = 0; i < size; i++)
    {
        float in_l = in[0][i];
        float in_r = mono_in ? in_l : in[1][i];

        // Runs in bypass too: the looper never stops listening.
        float l, r;
        engine.Process(in_l, in_r, &l, &r);
        limiter.Process(&l, &r);
        float dry_l = dry_kill ? 0.0f : in_l;
        float dry_r = dry_kill ? 0.0f : in_r;
        bypass_fade.Process(bypassed, dry_l, dry_r, &l, &r);
        out[0][i] = l;
        out[1][i] = r;

        peaks[0] = fmaxf(peaks[0], fmaxf(fabsf(in_l), fabsf(in_r)));
        peaks[3] = fmaxf(peaks[3], fmaxf(fabsf(l), fabsf(r)));
    }
    float wet_peak, loop_peak;
    engine.TakePeaks(&wet_peak, &loop_peak);
    peaks[1] = fmaxf(peaks[1], wet_peak);
    peaks[2] = fmaxf(peaks[2], loop_peak);
    sample_count += size;

    CollectTrace();
    if(want_telemetry)
    {
        FillTelemetry();
        want_telemetry = false;
    }
    cpu_meter.OnBlockEnd();
}

int main(void)
{
    hw.Init(true); // 480 MHz: the core can tick 1.33 times a sample at 64k
    hw.SetAudioBlockSize(4);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    float sr = hw.AudioSampleRate();
    engine.Init(sr,
                {loop_buffer,
                 kLoopFrames,
                 delay_buffer,
                 kDelayFrames,
                 slip_buffer,
                 kSlipFrames,
                 reverb_buffer,
                 kReverbFrames});
    tempo.Init(sr);
    ramp.Init(sr / hw.AudioBlockSize());
    synth.Init(sr / hw.AudioBlockSize());
    limiter.Init(sr);
    bypass_fade.Init(sr);
    cpu_meter.Init(sr, hw.AudioBlockSize());
    web.Init(kNumParams);
    web_out.Init(web_buffer);
    for(size_t i = 0; i < kOverviewColumns; i++)
        overview[i] = 0;

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
    };
    const float position_colours[3][3] = {
        {1.0f, 0.0f, 0.0f}, // left: red
        {0.0f, 1.0f, 0.0f}, // middle: green
        {0.0f, 0.0f, 1.0f}, // right: blue
    };
    uint32_t loop_starts = 0, dark_until = 0, overview_next = 0;
    while(1)
    {
        uint32_t now = System::GetNow();

        // LED 1: mono in flash, else the two channels, dark briefly at each
        // pass of the loop; cyan pulsing in Synth Mode.
        uint32_t starts = engine.LoopStarts();
        if(starts != loop_starts)
        {
            loop_starts = starts;
            dark_until  = now + 60;
        }
        bool playing = engine.LooperPlaying();
        if(now < mono_flash_until)
        {
            if(mono_in)
                hw.led1.Set(0.0f, 1.0f, 1.0f);
            else
                hw.led1.Set(0.0f, 0.0f, 0.0f);
        }
        else if(synth.Active())
        {
            float pulse = 0.4f + 0.6f * synth.Gain();
            hw.led1.Set(0.0f, pulse, pulse);
        }
        else if(playing && now < dark_until)
            hw.led1.Set(0.0f, 0.0f, 0.0f);
        else if(Freeze())
            hw.led1.Set(1.0f, 1.0f, 1.0f);
        else
        {
            float red = playing ? 1.0f : 0.0f;
            if(engine.Overdubbing() && (now / 80) % 2)
                red = 0.0f;
            hw.led1.Set(red, wet_on ? 1.0f : 0.0f, 0.0f);
        }

        // LED 2: page colour, or the page's toggle just after a change.
        int          page = pages.Page();
        const float *c    = now < toggle_flash_until ? position_colours[toggles[page]]
                                                     : page_colours[page];
        hw.led2.Set(c[0], c[1], c[2]);
        hw.UpdateLeds();

        RefreshOverview(&overview_next);
        SendWeb(now);
        System::Delay(10);
    }
}

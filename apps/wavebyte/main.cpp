// Wavebyte - an 8-voice wavetable synth for the Daisy Pod (Daisy Seed 3)
//
// Our own take on the Waldorf Protein: two wavetable oscillators per voice
// reading 8-bit waves with no interpolation, as the Microwave's chip did
// (or a Clean mode), Dirt's noises, a filter after the Protein's model of
// the CEM chip (or a Drive in its place), three envelopes, two LFOs, an
// 8-slot mod matrix, Flavour and a model of each voice's DAC, and eight
// voices, then two effect slots. Play it over MIDI (MPE too), or with
// button 1.
//
// The two knobs are shared across eleven pages; press the encoder to change
// page (LED 2 shows which). Like the Protein's panel, each knob has a
// second job under Shift (button 2): hold Shift while turning, or tap it to
// keep it on (LED 1 glows blue) and tap again to let go. After a change of
// page or Shift a knob does nothing until it is moved, so values don't
// jump. Turning the encoder sets the page's choice (on the Filter page,
// with Shift, the Dirt type; on the FX page, the routing). On the FX page,
// as on the Protein, knob 1 picks the slot's effect and knob 2 sets its
// Amount; with Shift, knob 1 picks one of its parameters and knob 2 sets
// its Value.
//
//   Page (LED 2)            Knob 1 / Shift      Knob 2 / Shift      Encoder turn
//   1 Osc 1 (red)           Wave / Level        Env Amnt / Vel      Table
//   2 Osc 2 (orange)        Wave / Level        Env Amnt / Vel      Table
//   3 Pitch (yellow)        Semitone 2 / 1      Detune 2 / 1        Voice: Poly, Mono, Legato
//   4 Filter (lime)         Cutoff / Keytrack   Resonance / Dirt    LP 24, LP 12, HP, Drive, Off
//                                                                   With Shift: Dirt type: Static,
//                                                                   Crackle, Geiger, Click, Burst 1-3,
//                                                                   Ring Mod
//   5 Env & Glide (green)   Filter Env / Vel    Glide / Bend Range  Glide: Onset, Legato
//   6 Envelopes (aqua)      Attack / Sustain    Decay / Release     Envelope: Wave, Filter, Amp
//   7 Amp (azure)           Volume / Vel Amnt   Pan / Master        Pan: Fix, Rand 1-5
//   8 Character (blue)      Flavour             DAC                 Osc: 8-bit, Clean
//                           (Shift does nothing here)
//   9 LFOs (violet)         Speed / Ratio       Shape / Mode        LFO: 1, 2
//   10 Mod Matrix (purple)  Source / Target     Amount / Amount     Slot: 1-8
//   11 FX (pink)            Type / Param        Amount / Value      Slot: FX 1, FX 2
//                                                                   With Shift: routing: Dry,
//                                                                   FX 1 > FX 2, FX 2 only
//
//   Page 12 (LED 2: the slot) Presets. Encoder turn - select a slot, loading
//                            its preset; an empty one keeps the sound.
//                            Encoder long press - save to the slot. LED 2
//                            shows the slot's colour: dim if empty,
//                            blinking off while modified, flickering white
//                            on a save. The knobs do nothing here.
//
//   Encoder long press - all notes off (LED 1 flashes red), or on the
//                        preset page, save
//   Button 1           - Play: sounds the play note while held. Turn the
//                        encoder while holding it to change the note (by
//                        octaves with Shift)
//   Button 2           - Shift
//
// With Drive as the filter type, Cutoff sets the amount and Resonance the
// curve: PNP, Tube, PickUp, Diode, Crunch.
//
// On the FX page LED 2 flashes what knob 1 picks: the effect, or with
// Shift, the parameter (see the README for each effect's list).
//
// LED 1 shows the last voice's amp envelope, blue with Shift on. Turning
// the encoder flashes LED 2 with the choice.
//
// MIDI on any channel, from the TRS input or USB: notes (with release
// velocity), pitch bend, channel and poly pressure, sustain (CC 64), the
// mod matrix's controllers (CC 1, 2, 11, 22-31), the fixed CC map below
// (kCcMap), all notes off (CC 120 and 123), program change 1-8 (sent as
// 0-7) for preset slots 1-8, and MIDI clock for Clocked LFOs. With MPE
// on (the web page), channel 1 is the master channel and each other
// channel's bend, CC 74 and pressure belong to its note.
//
// Over USB MIDI it also talks to the web control surface (web/, `make
// web`), streaming the output level, CPU and traces of the last voice
// played while the page is open, with the presets.
//
// Presets (common/presets.h) hold the sound. Master and the play note are
// saved on their own a few seconds after they change. At power-up they
// come back, then the preset last selected.
//
// Hold both buttons while powering up (both LEDs flash white) for a factory
// reset: this app's presets and saved settings are erased. The web page
// does the same while running.

#include "daisy_pod.h"
#include "colour.h"
#include "factory_reset.h"
#include "fx.h"
#include "limiter.h"
#include "paged_knobs.h"
#include "presets.h"
#include "press_hold.h"
#include "qspi_flash.h"
#include "sdram_timing.h"
#include "stack_meter.h"
#include "synth_engine.h"
#include "tempo.h"
#include "util/CpuLoadMeter.h"
#include "wavetables.h"
#include "web_link.h"
#include "web_view.h"

using namespace daisy;
using namespace web_view;

Wavetables::Wave DSY_SDRAM_BSS wave_memory[Wavetables::kTables * Wavetables::kWaves];
float DSY_SDRAM_BSS            fx_slow[FxChain::kSlowFloats]; // the delay's lines
// The reverb, chorus, flanger and look-ahead lines are read many times a
// sample, so they go in RAM_D2, through the linker script's .heap section
// (libDaisy gives nothing else there). Leave room: malloc's heap starts
// where .heap ends and has no limit check.
float __attribute__((section(".heap"))) fx_fast[FxChain::kFastFloats];
uint8_t DSY_SDRAM_BSS          web_buffer[weblink::kMaxMessage];

DaisyPod       hw;
MidiUsbHandler usb_midi;
Wavetables     tables;
SynthEngine    engine;
FxChain        effects;
Tempo          tempo; // for Clocked LFOs: the Tempo setting, or MIDI clock
Limiter        limiter; // eight voices can pass full scale
CpuLoadMeter   cpu_meter;
QspiFlash      flash(hw.seed.qspi);

// The presets' arrays are in SDRAM: DTCM is nearly full, and they're only
// touched by the main loop and by Apply when a preset is waiting.
PresetsMemory<kNumParams> DSY_SDRAM_BSS preset_memory;
PresetsIn<kNumParams>                   presets(&preset_memory);

constexpr int kNumKnobPages = 11;
constexpr int kPresetPage   = kNumKnobPages; // after the knob pages; its knobs do nothing
constexpr int kNumPages     = kNumKnobPages + 1;
constexpr int kNumKnobs     = 2;
constexpr int kFilterPage   = 3;
constexpr int kEnvPage      = 5;
constexpr int kCharPage     = 7; // one layer: Shift does nothing
constexpr int kLfoPage      = 8;
constexpr int kModPage      = 9;
constexpr int kFxPage       = 10;

// Layers of the knobs: each page's, and its Shift layer; the envelope and
// LFO pages have a pair per envelope or LFO, the mod page a pair per slot,
// the FX page a pair per slot, and the Character page has no Shift layer.
// Up to the mod page, a layer's knobs are parameters 2 x layer and
// 2 x layer + 1 (web_view.h).
constexpr int kCharLayer   = 2 * (kCharPage + 2);
constexpr int kLfoLayer    = kCharLayer + 1;
constexpr int kModLayer    = kLfoLayer + 4;
constexpr int kFxLayer     = kModLayer + 2 * mod::kSlots;
constexpr int kPresetLayer = kFxLayer + 4;
constexpr int kNumLayers   = kPresetLayer + 1;
static_assert(2 * kModLayer == kParamMod1Source, "the knob layers and parameters are out of step");

// sub: the page's envelope, LFO or slot.
int Layer(int page, bool shift, int sub)
{
    switch(page)
    {
        case kPresetPage: return kPresetLayer;
        case kEnvPage: return 2 * kEnvPage + 2 * sub + shift;
        case kCharPage: return kCharLayer;
        case kLfoPage: return kLfoLayer + 2 * sub + shift;
        case kModPage: return kModLayer + 2 * sub + shift;
        case kFxPage: return kFxLayer + 2 * sub + shift;
        default: return 2 * (page < kEnvPage ? page : page + 2) + shift;
    }
}

// Where a knob parameter lives. A mod slot's are Source and Amount, then
// Target under Shift, whose knob 2 is a copy of Amount; an FX slot's is
// only its Type (knob 1), as its other knobs show the effects' values (see
// Mirror).
void KnobOf(int param, int *layer, int *knob)
{
    if(param >= kParamFx1Type)
    {
        *layer = kFxLayer + 2 * (param - kParamFx1Type);
        *knob  = 0;
        return;
    }
    if(param < kParamMod1Source)
    {
        *layer = param / 2;
        *knob  = param % 2;
        return;
    }
    int slot = (param - kParamMod1Source) / 3, which = (param - kParamMod1Source) % 3;
    *layer   = kModLayer + 2 * slot + (which == 2);
    *knob    = which == 1;
}

// Raw knob positions (0..1) per layer, initial values chosen to sound sensible.
PagedKnobs<kNumLayers, kNumKnobs> knobs({
    {0.0f, 0.5f},   // wave 1, env amount 1
    {0.8f, 0.5f},   // level 1, vel 1
    {0.0f, 0.5f},   // wave 2, env amount 2
    {0.6f, 0.5f},   // level 2, vel 2
    {0.5f, 0.53f},  // semitone 2, detune 2: a few cents up
    {0.5f, 0.5f},   // semitone 1, detune 1
    {0.7f, 0.2f},   // cutoff, resonance
    {0.625f, 0.0f}, // track 50%, dirt
    {0.7f, 0.0f},   // filter env, glide
    {0.5f, 2.0f / 12.0f}, // filter vel, bend range 2
    {0.0f, 0.5f},   // wave env: attack, decay
    {0.0f, 0.5f},   // sustain, release
    {0.0f, 0.45f},  // filter env
    {0.3f, 0.45f},
    {0.0f, 0.5f},   // amp env
    {1.0f, 0.4f},
    {0.8f, 0.5f},   // volume, pan centred
    {0.5f, 0.8f},   // amp vel, master
    {0.0f, 0.3f},   // flavour, dac
    {0.724f, 0.5f / 7},  // lfo 1: 5 Hz, sine
    {10.5f / 22, 0.5f / 3}, // 1/4, Free Voice
    {0.422f, 1.5f / 7},  // lfo 2: 0.5 Hz, triangle
    {4.5f / 22, 1.5f / 3},  // 1/1, Free Global
    {5.5f / 35, 0.556f}, // slot 1: LFO 1 x mod wheel, a little
    {3.5f / 27, 0.556f}, // ... to Pitch: vibrato on the wheel
    {0.0f, 0.5f}, {0.0f, 0.5f}, // slots 2-8: off
    {0.0f, 0.5f}, {0.0f, 0.5f},
    {0.0f, 0.5f}, {0.0f, 0.5f},
    {0.0f, 0.5f}, {0.0f, 0.5f},
    {0.0f, 0.5f}, {0.0f, 0.5f},
    {0.0f, 0.5f}, {0.0f, 0.5f},
    {0.0f, 0.5f}, {0.0f, 0.5f},
    {0.05f, 0.0f},  // fx 1: off; Amount (shown)
    {0.0f, 0.0f},   // the parameter picked; its Value (shown)
    {0.95f, 0.0f},  // fx 2: Reverb
    {0.0f, 0.0f},
    {0.0f, 0.0f},   // presets: unused
});

// ---------------------------------------------------------------- State
//
// What the Pod and the web page set. Only the audio callback writes it;
// the main loop reads it for the LEDs and the page.

// Each knob page's encoder choice: tables, voice mode, filter type, glide
// mode, the envelope shown, pan mode, oscillator mode, the LFO shown, the
// mod slot shown, the FX slot shown.
constexpr int kChoiceCounts[kNumKnobPages] = {Wavetables::kTables, Wavetables::kTables,
                                              SynthEngine::kNumVoiceModes, VoiceFilter::kNumTypes,
                                              SynthEngine::kNumGlideModes, Voice::kNumEnvs,
                                              SynthEngine::kNumPanModes, SynthEngine::kNumOscModes,
                                              2, mod::kSlots, 2};
int  choices[kNumKnobPages] = {0, 0, SynthEngine::kPoly, VoiceFilter::kLp24,
                               SynthEngine::kGlideOnset, Voice::kAmpEnv, 0, SynthEngine::kOsc8Bit,
                               0, 0, 0};
int  dirt_type = Dirt::kStatic; // the Filter page's choice with Shift
int  fx_routing = kFxSerial;    // the FX page's choice with Shift
float fx_values[kNumFxParams];  // the effects' values, by type (fx_params.h)
bool limit[2]  = {};
bool fixed[2]  = {};
bool mpe       = false;
bool clock_restart = false; // MIDI Start: Clocked LFOs back to the start
uint32_t sample_count = 0;  // the callback's clock, for the tempo
float tempo_raw = 0.4f; // 40-240 BPM: 120
int  page      = 0;
bool shift_latched = false;
bool shift_held    = false;
bool shift_used    = false; // a knob moved while Shift was held: no toggle on release
float shift_knobs[kNumKnobs] = {};

constexpr int kPlayLow = 24, kPlayHigh = 96; // the play note's range
int  play_note    = 60;
int  play_sounding = -1; // the note button 1 or the page holds down
bool web_play      = false;

PressHold encoder_press(600); // hold: all notes off, or on the preset page, save
PressHold shift_press(400);   // a tap keeps Shift on, or lets it go

// Read by the main loop for the LEDs: flash until these times (ms), the
// choice's colour.
volatile uint32_t choice_flash_until = 0;
volatile float    choice_flash_hue   = 0.0f;
volatile uint32_t panic_flash_until  = 0;
volatile float    amp_now            = 0.0f;

bool Shift()
{
    return shift_latched || shift_held;
}

constexpr float kMinBpm = 40.0f, kBpmRange = 200.0f;

float KnobValue(int param)
{
    int layer, knob;
    KnobOf(param, &layer, &knob);
    return knobs.Value(layer, knob);
}

void SetKnob(int param, float value)
{
    int layer, knob;
    KnobOf(param, &layer, &knob);
    knobs.Set(layer, knob, value);
}

float PlayNoteRaw()
{
    return (float)(play_note - kPlayLow) / (kPlayHigh - kPlayLow);
}

// Button 1 or the page: the play note down or up.
void PlayGate(bool down)
{
    if(play_sounding >= 0)
        engine.NoteOff(play_sounding);
    play_sounding = -1;
    if(down)
    {
        play_sounding = play_note;
        engine.NoteOn(play_note, 100);
    }
}

void SetPlayNote(int note)
{
    play_note = note < kPlayLow ? kPlayLow : (note > kPlayHigh ? kPlayHigh : note);
    if(play_sounding >= 0 && play_sounding != play_note)
        PlayGate(true);
}

// ---------------------------------------------------------------- Web link
//
// The page in web/ (see web_view.h). It sets values through SysEx, read in
// the audio callback; the main loop sends it what it needs.

weblink::Session web;
weblink::Writer  web_out;
// The main loop's alone, so in SDRAM, out of DTCM.
int DSY_SDRAM_BSS web_state[kNumParams]; // values in the last state sent
int DSY_SDRAM_BSS web_raw[kNumParams];   // ... and the values now

// The audio callback appends to this ring; the main loop reads what's new
// for each frame. A power of two, longer than a frame takes.
constexpr uint32_t  kTraceRing = 64;
float DSY_SDRAM_BSS trace_ring[kTraceRing][kNumTraces];
volatile uint32_t   trace_written = 0; // points so far
uint32_t            trace_read    = 0; // main loop: points sent
int                 trace_samples = 0, trace_period = 96;

volatile float peak        = 0.0f; // out
volatile bool  reset_peaks = false;
volatile float stack_free  = 0.0f; // KB the stack has never used, from the main loop

Telemetry DSY_SDRAM_BSS telemetry; // the main loop's copy

int Clamp(int value, int n)
{
    return value < 0 ? 0 : (value >= n ? n - 1 : value);
}

// LED 2 flashes the new choice if its page is showing.
void SetChoice(int p, int value, uint32_t now_ms)
{
    int n      = kChoiceCounts[p];
    choices[p] = Clamp(value, n);
    if(p == page)
    {
        choice_flash_hue   = (float)choices[p] / n;
        choice_flash_until = now_ms + 1000;
    }
}

void SetFxRouting(int value, uint32_t now_ms)
{
    fx_routing = Clamp(value, kNumFxRoutings);
    if(page == kFxPage)
    {
        choice_flash_hue   = (float)fx_routing / kNumFxRoutings;
        choice_flash_until = now_ms + 1000;
    }
}

void SetDirtType(int value, uint32_t now_ms)
{
    dirt_type = Clamp(value, Dirt::kNumTypes);
    if(page == kFilterPage)
    {
        choice_flash_hue   = (float)dirt_type / Dirt::kNumTypes;
        choice_flash_until = now_ms + 1000;
    }
}

// A value from the page, in the audio callback.
void SetParam(int index, int raw, uint32_t now_ms)
{
    switch(index)
    {
        case kParamLimit1: limit[0] = raw != 0; break;
        case kParamLimit2: limit[1] = raw != 0; break;
        case kParamFixed1: fixed[0] = raw != 0; break;
        case kParamFixed2: fixed[1] = raw != 0; break;
        case kParamPlayNote:
            SetPlayNote(kPlayLow + (int)(raw / 16383.0f * (kPlayHigh - kPlayLow) + 0.5f));
            break;
        case kParamPlay:
            web_play = raw != 0;
            PlayGate(web_play);
            break;
        case kParamDirtType: SetDirtType(raw, now_ms); break;
        case kParamFxRouting: SetFxRouting(raw, now_ms); break;
        case kParamMpe: mpe = raw != 0; break;
        case kParamTempo:
            tempo_raw = raw / 16383.0f;
            tempo.SetBpm(kMinBpm + tempo_raw * kBpmRange);
            break;
        default:
            if(index < kNumKnobParams)
                SetKnob(index, raw / 16383.0f);
            else if(index >= kParamFxFirst && index <= kParamFxLast)
                fx_values[index - kParamFxFirst] = raw / 16383.0f;
            else if(index >= kFirstChoice && index < kFirstChoice + kNumKnobPages)
                SetChoice(index - kFirstChoice, raw, now_ms);
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
        case kParamLimit1: return limit[0];
        case kParamLimit2: return limit[1];
        case kParamFixed1: return fixed[0];
        case kParamFixed2: return fixed[1];
        case kParamPlayNote: return (int)(PlayNoteRaw() * 16383.0f + 0.5f);
        case kParamPlay: return play_sounding >= 0;
        case kParamDirtType: return dirt_type;
        case kParamFxRouting: return fx_routing;
        case kParamMpe: return mpe;
        case kParamTempo: return (int)(tempo_raw * 16383.0f + 0.5f);
        default:
            if(index < kNumKnobParams)
                return (int)(KnobValue(index) * 16383.0f + 0.5f);
            if(index >= kParamFxFirst && index <= kParamFxLast)
                return (int)(fx_values[index - kParamFxFirst] * 16383.0f + 0.5f);
            if(index >= kFirstChoice && index < kFirstChoice + kNumKnobPages)
                return choices[index - kFirstChoice];
            return 0;
    }
}

// Once per block, in the audio callback: a trace point every kTracePeriodMs.
void Collect(size_t size)
{
    SynthEngine::Probe p = engine.GetProbe();
    amp_now              = p.amp;
    trace_samples += (int)size;
    if(trace_samples >= trace_period)
    {
        float *point        = trace_ring[trace_written & (kTraceRing - 1)];
        point[kTraceWave1]  = p.wave[0];
        point[kTraceWave2]  = p.wave[1];
        point[kTraceCutoff] = p.cutoff * 0.001f;
        point[kTraceAmp]    = 100.0f * p.amp;
        point[kTraceDrift]  = p.drift;
        point[kTraceLfo1]   = 100.0f * p.lfo[0];
        point[kTraceLfo2]   = 100.0f * p.lfo[1];
        point[kTraceVoices] = (float)p.voices;
        point[kTraceStack]  = stack_free;
        trace_written       = trace_written + 1; // after the point is in place
        trace_samples       = 0;
    }
}

// In the main loop: what's new since the last frame. The callback only
// overwrites ring entries far older than these.
void ReadTelemetry()
{
    Telemetry &t = telemetry;
    t.peak       = peak;
    reset_peaks  = true;

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
    int *raw     = web_raw;
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
// Everything on any channel, from the TRS input or USB.

void Panic(uint32_t now_ms)
{
    engine.AllNotesOff();
    engine.Sustain(false);
    play_sounding     = -1;
    web_play          = false;
    panic_flash_until = now_ms + 300;
}

// The fixed CC map: the Protein maps CCs by learning, which the Pod can't
// do comfortably. It keeps clear of the mod matrix's controllers (1, 2,
// 11, 22-31, 64, and 74 with MPE) and 120-127.
struct CcParam
{
    uint8_t cc;
    uint8_t param;
};
constexpr CcParam kCcMap[] = {
    {5, kParamGlide},          {7, kParamVolume},        {10, kParamPan},
    {14, kParamMod1Source + 1}, {15, kParamMod1Source + 4}, {16, kParamMod1Source + 7},
    {17, kParamMod1Source + 10}, {18, kParamMod1Source + 13}, {19, kParamMod1Source + 16},
    {20, kParamMod1Source + 19}, {21, kParamMod1Source + 22}, // mod amounts 1-8
    {52, kParamWave1},         {53, kParamEnvAmt1},      {54, kParamLevel1},
    {55, kParamVel1},          {56, kParamWave2},        {57, kParamEnvAmt2},
    {58, kParamLevel2},        {59, kParamVel2},         {60, kParamSemitone1},
    {61, kParamDetune1},       {62, kParamSemitone2},    {63, kParamDetune2},
    {70, kParamTrack},         {71, kParamResonance},    {74, kParamCutoff}, // not with MPE
    {76, kParamDirt},          {77, kParamFilterEnv},    {78, kParamFilterVel},
    {79, kParamBend},          {85, kParamAmpVel},       {86, kParamFlavour},
    {87, kParamDac},           {102, kParamWaveAttack},  {103, kParamWaveDecay},
    {104, kParamWaveSustain},  {105, kParamWaveRelease}, {106, kParamFilterAttack},
    {107, kParamFilterDecay},  {108, kParamFilterSustain}, {109, kParamFilterRelease},
    {110, kParamAmpAttack},    {111, kParamAmpDecay},    {112, kParamAmpSustain},
    {113, kParamAmpRelease},   {114, kParamLfo1Speed},   {115, kParamLfo1Shape},
    {116, kParamLfo2Speed},    {117, kParamLfo2Shape},
};
// And choices: CC 3 and 9 pick table 1 and 2 by number (0-63), CC 118 the
// filter type and CC 119 the Dirt type, split evenly across 0-127.

// 0-127 to 0..1, with 64 exactly in the middle (no amount for a centred
// bipolar knob).
float CcValue(int v)
{
    return v <= 64 ? (float)v / 128.0f : 0.5f + (float)(v - 64) / 126.0f;
}

void HandleCc(int channel, int cc, int value, uint32_t now_ms)
{
    float v = (float)value / 127.0f;
    if(cc == 120 || cc == 123)
    {
        Panic(now_ms);
        return;
    }
    engine.Control(channel, cc, v); // the matrix's controllers, and sustain
    switch(cc)
    {
        case 3: SetChoice(0, value, now_ms); return;
        case 9: SetChoice(1, value, now_ms); return;
        case 118: SetChoice(3, value * VoiceFilter::kNumTypes / 128, now_ms); return;
        case 119: SetDirtType(value * Dirt::kNumTypes / 128, now_ms); return;
        case 74:
            if(mpe)
                return;
            break;
        default: break;
    }
    for(const CcParam &m : kCcMap)
        if(m.cc == cc)
            SetKnob(m.param, CcValue(value));
}

void HandleMidi(MidiEvent e, uint32_t now, uint32_t now_ms)
{
    switch(e.type)
    {
        case NoteOn:
        {
            NoteOnEvent n = e.AsNoteOn();
            engine.NoteOn(n.note, n.velocity, n.channel);
            break;
        }
        case NoteOff:
        {
            NoteOffEvent n = e.AsNoteOff();
            engine.NoteOff(n.note, n.velocity, n.channel);
            break;
        }
        case PitchBend: engine.PitchBend(e.channel, e.AsPitchBend().value / 8192.0f); break;
        case ChannelPressure: engine.Pressure(e.channel, e.AsChannelPressure().pressure / 127.0f); break;
        case PolyphonicKeyPressure:
        {
            PolyphonicKeyPressureEvent p = e.AsPolyphonicKeyPressure();
            engine.PolyPressure(p.note, p.pressure / 127.0f);
            break;
        }
        case ControlChange:
        {
            ControlChangeEvent cc = e.AsControlChange();
            HandleCc(cc.channel, cc.control_number, cc.value, now_ms);
            break;
        }
        case ProgramChange: presets.Select(e.AsProgramChange().program); break;
        case SystemRealTime:
            switch(e.srt_type)
            {
                case TimingClock: tempo.MidiTick(now); break;
                case Start:
                    tempo.MidiStart();
                    clock_restart = true;
                    break;
                case Continue: tempo.MidiContinue(); break;
                case Stop: tempo.MidiStop(); break;
                default: break;
            }
            break;
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

// The patch, from the knobs and choices.
SynthEngine::Patch MakePatch()
{
    auto k = [](int param) { return KnobValue(param); };

    SynthEngine::Patch p;
    const int wave[2] = {kParamWave1, kParamWave2};
    const int env[2]  = {kParamEnvAmt1, kParamEnvAmt2};
    const int level[2] = {kParamLevel1, kParamLevel2};
    const int vel[2]  = {kParamVel1, kParamVel2};
    const int semi[2] = {kParamSemitone1, kParamSemitone2};
    const int det[2]  = {kParamDetune1, kParamDetune2};
    for(int o = 0; o < 2; o++)
    {
        SynthEngine::Patch::Osc &po = p.osc[o];
        po.table    = choices[o];
        po.wave     = k(wave[o]);
        po.env      = k(env[o]);
        po.velocity = k(vel[o]);
        po.level    = k(level[o]);
        po.semitone = k(semi[o]);
        po.detune   = k(det[o]);
        po.limit    = limit[o];
        po.fixed    = fixed[o];
    }
    p.voice_mode = choices[2];
    p.glide      = k(kParamGlide);
    p.glide_mode = choices[4];
    p.bend_range = k(kParamBend);
    p.filter     = choices[3];
    p.cutoff     = k(kParamCutoff);
    p.resonance  = k(kParamResonance);
    p.filter_env = k(kParamFilterEnv);
    p.filter_vel = k(kParamFilterVel);
    p.track      = k(kParamTrack);
    p.dirt       = k(kParamDirt);
    p.dirt_type  = dirt_type;
    p.flavour    = k(kParamFlavour);
    p.dac        = k(kParamDac);
    p.osc_mode   = choices[kCharPage];
    for(int e = 0; e < Voice::kNumEnvs; e++)
        for(int s = 0; s < 4; s++)
            p.env[e][s] = k(kParamWaveAttack + 4 * e + s);
    p.volume       = k(kParamVolume);
    p.amp_velocity = k(kParamAmpVel);
    p.pan          = k(kParamPan);
    p.pan_mode     = choices[6];
    for(int i = 0; i < 2; i++)
    {
        int first    = kParamLfo1Speed + 4 * i;
        p.lfo[i].speed = k(first);
        p.lfo[i].shape = k(first + 1);
        p.lfo[i].ratio = k(first + 2);
        p.lfo[i].mode  = k(first + 3);
    }
    for(int i = 0; i < mod::kSlots; i++)
    {
        int first        = kParamMod1Source + 3 * i;
        p.mod[i].source = k(first);
        p.mod[i].amount = k(first + 1);
        p.mod[i].target = k(first + 2);
    }
    p.mpe = mpe;
    return p;
}

FxType FxSlotType(int slot)
{
    return (FxType)SynthEngine::Pick(KnobValue(kParamFx1Type + slot), kNumFxTypes);
}

// A knob that shows a value kept elsewhere (*real): the layer keeps a copy,
// kept in step both ways, the knob moving it, or the page, MIDI or a preset
// setting the real one. A new layer or target (`which`) takes the real
// value and lets go of the knob until it moves. real: nullptr for none.
float mirror_shown = -1.0f;
int   mirror_which = -1;

void Mirror(int layer, int knob, float *real, int which, bool entered)
{
    if(!real)
    {
        mirror_which = -1;
        return;
    }
    float shown = knobs.Value(layer, knob);
    if(entered || which != mirror_which || (shown == mirror_shown && *real != shown))
        knobs.Set(layer, knob, *real);
    else if(shown != mirror_shown)
        *real = shown;
    mirror_shown = knobs.Value(layer, knob);
    mirror_which = which;
}

// Knob 2 on a mod slot's Shift layer is Amount again; on the FX page it's
// the slot's effect's Amount (its first value), or with Shift, the Value
// of the parameter knob 1 picks.
void MirrorKnobs(int layer, bool entered)
{
    if(page == kModPage && Shift())
    {
        int   main = layer - 1;
        float real = knobs.Value(main, 1);
        Mirror(layer, 1, &real, main, entered);
        if(real != knobs.Value(main, 1))
            knobs.Set(main, 1, real); // not showing, so it stays as it is
    }
    else if(page == kFxPage)
    {
        FxType type  = FxSlotType(choices[kFxPage]);
        int    count = kFxCount[type];
        int    which = count == 0 ? -1 : kFxFirst[type] + (Shift() ? SynthEngine::Pick(knobs.Value(layer, 0), count) : 0);
        Mirror(layer, 1, which < 0 ? nullptr : &fx_values[which], 1024 * layer + which, entered);
    }
    else
        Mirror(layer, 1, nullptr, -1, entered);
}

// With no display, LED 2 flashes what knob 1 picks on the FX page: the
// effect (Hue(type / 10)), or with Shift the parameter, along its list.
int fx_pick_layer = -1, fx_pick = -1;

void FlashFxPick(int layer, uint32_t now_ms)
{
    if(page != kFxPage)
    {
        fx_pick_layer = -1;
        return;
    }
    FxType type  = FxSlotType(choices[kFxPage]);
    int    count = kFxCount[type];
    int    pick  = Shift() ? (count ? SynthEngine::Pick(knobs.Value(layer, 0), count) : 0) : type;
    if(layer == fx_pick_layer && pick != fx_pick)
    {
        choice_flash_hue   = Shift() ? (float)pick / (float)count : (float)type / kNumFxTypes;
        choice_flash_until = now_ms + 1000;
    }
    fx_pick_layer = layer;
    fx_pick       = pick;
}

FxChain::Settings MakeFxSettings()
{
    FxChain::Settings s;
    s.type[0]          = FxSlotType(0);
    s.type[1]          = FxSlotType(1);
    s.routing          = (FxRouting)fx_routing;
    s.values           = fx_values;
    s.samples_per_beat = tempo.SamplesPerBeat();
    return s;
}

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    cpu_meter.OnBlockStart();
    hw.ProcessAllControls();
    uint32_t now_ms = System::GetNow();
    uint32_t now    = sample_count;

    hw.midi.Listen();
    usb_midi.Listen();
    while(hw.midi.HasEvents())
        HandleMidi(hw.midi.PopEvent(), now, now_ms);
    while(usb_midi.HasEvents())
        HandleMidi(usb_midi.PopEvent(), now, now_ms);

    // The tempo moves Clocked LFOs on: beats since the last block.
    float beats_before = tempo.BeatPos();
    tempo.Update(now);
    float beats = tempo.BeatPos() - beats_before;
    if(beats < 0.0f)
        beats += (float)Tempo::kWrapBeats;
    engine.Clock(clock_restart ? 0.0f : beats, clock_restart);
    clock_restart = false;

    const DaisyPod::Knob knob_ids[kNumKnobs] = {DaisyPod::KNOB_1, DaisyPod::KNOB_2};
    float                knob[kNumKnobs];
    for(int i = 0; i < kNumKnobs; i++)
        knob[i] = hw.GetKnobValue(knob_ids[i]);

    // Button 1: Play.
    if(hw.button1.RisingEdge())
        PlayGate(true);
    if(hw.button1.FallingEdge() && !web_play)
        PlayGate(false);

    // Button 2: Shift while held; a tap with no knob turned keeps it on,
    // or lets it go.
    if(hw.button2.RisingEdge())
    {
        shift_held = true;
        shift_used = false;
        for(int i = 0; i < kNumKnobs; i++)
            shift_knobs[i] = knob[i];
    }
    if(shift_held)
        for(int i = 0; i < kNumKnobs; i++)
            if(fabsf(knob[i] - shift_knobs[i]) > 0.02f)
                shift_used = true;
    if(hw.button2.FallingEdge())
        shift_held = false;
    if(shift_press.Update(hw.button2.Pressed(), hw.button2.TimeHeldMs()) == PressHold::Event::kTap
       && !shift_used)
        shift_latched = !shift_latched;

    // Long press saves on the preset page, and stops every note elsewhere,
    // as soon as the hold is long enough; a short press changes page on
    // release.
    PressHold::Event enc = encoder_press.Update(hw.encoder.Pressed(), hw.encoder.TimeHeldMs());
    if(enc == PressHold::Event::kHold && page == kPresetPage)
        presets.Save();
    else if(enc == PressHold::Event::kHold)
        Panic(now_ms);
    else if(enc == PressHold::Event::kTap)
    {
        page               = (page + 1) % kNumPages;
        choice_flash_until = 0;
    }

    // Turning: the play note while button 1 is held, else the page's choice
    // (on the Filter page with Shift, the Dirt type).
    int inc = hw.encoder.Increment();
    if(inc != 0 && hw.button1.Pressed())
        SetPlayNote(play_note + inc * (Shift() ? 12 : 1));
    else if(inc != 0 && page == kPresetPage)
        presets.Step(inc);
    else if(inc != 0 && page == kFilterPage && Shift())
        SetDirtType(dirt_type + inc, now_ms);
    else if(inc != 0 && page == kFxPage && Shift())
        SetFxRouting(fx_routing + inc, now_ms);
    else if(inc != 0)
        SetChoice(page, choices[page] + inc, now_ms);
    presets.Apply();

    // The knobs work on this page's layer, with Shift's and the envelope's,
    // LFO's or slot's.
    int  sub     = page == kEnvPage || page == kLfoPage || page == kModPage || page == kFxPage ? choices[page] : 0;
    int  layer   = Layer(page, Shift(), sub);
    bool entered = layer != knobs.Page();
    if(entered)
        knobs.SetPage(layer, knob);
    knobs.Update(knob);
    MirrorKnobs(layer, entered);
    FlashFxPick(layer, now_ms);

    engine.SetPatch(MakePatch());
    effects.Set(MakeFxSettings(), size);

    if(reset_peaks)
    {
        peak        = 0.0f;
        reset_peaks = false;
    }
    float peak_out = peak;
    float master   = knobs.Value(kParamMaster / 2, kParamMaster % 2);
    master *= master;

    float  l[16], r[16];
    size_t done = 0;
    while(done < size)
    {
        size_t n = size - done < 16 ? size - done : 16;
        engine.Process(l, r, n);
        effects.Process(l, r, n);
        for(size_t i = 0; i < n; i++)
        {
            float ol = l[i] * master, or_ = r[i] * master;
            limiter.Process(&ol, &or_);
            out[0][done + i] = ol;
            out[1][done + i] = or_;
            peak_out         = fmaxf(peak_out, fmaxf(fabsf(ol), fabsf(or_)));
        }
        done += n;
    }
    peak = peak_out;
    (void)in;

    Collect(size);
    sample_count += (uint32_t)size;
    cpu_meter.OnBlockEnd();
}

int main(void)
{
    stack_meter::Paint();
    hw.Init(true); // 480 MHz
    TuneSdramTimings();
    for(int i = 0; i < kNumParams; i++)
        web_state[i] = web_raw[i] = 0;
    // Each voice's control maths (matrix, LFOs, envelopes, pitch, filter
    // coefficients) runs once per block, and at 8 voices it outweighs the
    // samples at block size 4. 16 runs it at 3 kHz, with 0.33 ms timing,
    // and the amplifier still ramps smoothly across each block.
    hw.SetAudioBlockSize(16);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    tables.Init(wave_memory);
    engine.Init(&tables, hw.AudioSampleRate(), hw.AudioBlockSize());
    effects.Init(fx_fast, fx_slow, hw.AudioSampleRate(), hw.AudioBlockSize());
    for(int i = 0; i < kNumFxParams; i++)
        fx_values[i] = kFxParams[i].initial;
    tempo.Init(hw.AudioSampleRate());
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
    presets.Init(&flash, kRegionWavebyte, kParams, kNumParams, ParamRaw, SetPresetParam, reset);

    // Every knob starts "not live" so the defaults above hold until a knob moves.
    hw.StartAdc();
    System::Delay(10); // let the ADC settle before taking the reference reading
    hw.ProcessAnalogControls();
    knobs.Latch({hw.GetKnobValue(DaisyPod::KNOB_1), hw.GetKnobValue(DaisyPod::KNOB_2)});

    hw.StartAudio(AudioCallback);

    while(1)
    {
        uint32_t now = System::GetNow();
        int      p   = page;
        float    r, g, b;

        // LED 1: red after all notes off, else the last voice's amp
        // envelope, blue with Shift on.
        if(now < panic_flash_until)
        {
            r = 1.0f;
            g = b = 0.0f;
        }
        else
        {
            float amp = amp_now;
            r = g = amp;
            b     = Shift() ? fmaxf(amp, 0.6f) : amp;
        }
        hw.led1.Set(r, g, b);

        // LED 2: the preset slot on the preset page, else the page colour,
        // or the page's choice just after a change.
        if(p == kPresetPage)
            presets.LedColour(now, &r, &g, &b);
        else if(now < choice_flash_until)
            Hue(choice_flash_hue, &r, &g, &b);
        else
            Hue((float)p / kNumKnobPages, &r, &g, &b);
        hw.led2.Set(r, g, b);
        hw.UpdateLeds();

        presets.Poll(now);
        stack_free = (float)stack_meter::Free() / 1024.0f;
        SendWeb(now);
        System::Delay(10);
    }
}

// Wavebyte - an 8-voice wavetable synth for the Daisy Pod (Daisy Seed 3)
//
// Our own take on the Waldorf Protein: two wavetable oscillators per voice
// reading 8-bit waves with no interpolation, as the Microwave's chip did,
// a filter after the Protein's model of the CEM chip (or a Drive in its
// place), three envelopes and eight voices. Play it over MIDI, or with
// button 1.
//
// The two knobs are shared across seven pages; press the encoder to change
// page (LED 2 shows which). Like the Protein's panel, each knob has a
// second job under Shift (button 2): hold Shift while turning, or tap it to
// keep it on (LED 1 glows blue) and tap again to let go. After a change of
// page or Shift a knob does nothing until it is moved, so values don't
// jump. Turning the encoder sets the page's choice.
//
//   Page (LED 2)        Knob 1 / Shift          Knob 2 / Shift         Encoder turn
//   1 Osc 1 (red)       Wave / Level            Env Amnt / Vel         Table
//   2 Osc 2 (yellow)    Wave / Level            Env Amnt / Vel         Table
//   3 Pitch (green)     Semitone 2 / 1          Detune 2 / 1           Voice: Poly, Mono, Legato
//   4 Filter (teal)     Cutoff / Keytrack       Resonance / Dirt       LP 24, LP 12, HP, Drive, Off
//   5 Env & Glide (blue) Filter Env / Vel       Glide / Bend Range     Glide: Onset, Legato
//   6 Envelopes (violet) Attack / Sustain       Decay / Release        Envelope: Wave, Filter, Amp
//   7 Amp (magenta)     Volume / Vel Amnt       Pan / Master           Pan: Fix, Rand 1-5
//
//   Page 8 (LED 2: the slot) Presets. Encoder turn - select a slot, loading
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
// LED 1 shows the last voice's amp envelope, blue with Shift on. Turning
// the encoder flashes LED 2 with the choice.
//
// MIDI on any channel, from the TRS input or USB: notes, pitch bend,
// sustain (CC 64), all notes off (CC 120 and 123), and program change 1-8
// (sent as 0-7) for preset slots 1-8.
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
#include "limiter.h"
#include "paged_knobs.h"
#include "presets.h"
#include "press_hold.h"
#include "qspi_flash.h"
#include "sdram_timing.h"
#include "synth_engine.h"
#include "util/CpuLoadMeter.h"
#include "wavetables.h"
#include "web_link.h"
#include "web_view.h"

using namespace daisy;
using namespace web_view;

Wavetables::Wave DSY_SDRAM_BSS wave_memory[Wavetables::kTables * Wavetables::kWaves];
uint8_t DSY_SDRAM_BSS          web_buffer[weblink::kMaxMessage];

DaisyPod       hw;
MidiUsbHandler usb_midi;
Wavetables     tables;
SynthEngine    engine;
Limiter        limiter; // eight voices can pass full scale
CpuLoadMeter   cpu_meter;
QspiFlash      flash(hw.seed.qspi);

PresetsFor<kNumParams> presets;

constexpr int kNumKnobPages = 7;
constexpr int kPresetPage   = kNumKnobPages; // after the knob pages; its knobs do nothing
constexpr int kNumPages     = kNumKnobPages + 1;
constexpr int kNumKnobs     = 2;
constexpr int kEnvPage      = 5;

// Layers of the knobs: each page's, and its Shift layer; the envelope page
// has a pair per envelope. A layer's knobs are parameters 2 x layer and
// 2 x layer + 1 (web_view.h).
constexpr int kNumLayers  = kNumKnobParams / kNumKnobs + 1; // and the preset page's
constexpr int kPresetLayer = kNumLayers - 1;

int Layer(int page, bool shift, int envelope)
{
    if(page == kPresetPage)
        return kPresetLayer;
    if(page == kEnvPage)
        return 2 * kEnvPage + 2 * envelope + shift;
    return 2 * (page < kEnvPage ? page : page + 2) + shift;
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
    {0.0f, 0.0f},   // presets: unused
});

// ---------------------------------------------------------------- State
//
// What the Pod and the web page set. Only the audio callback writes it;
// the main loop reads it for the LEDs and the page.

// Each knob page's encoder choice: tables, voice mode, filter type, glide
// mode, the envelope shown, pan mode.
constexpr int kChoiceCounts[kNumKnobPages] = {Wavetables::kTables, Wavetables::kTables,
                                              SynthEngine::kNumVoiceModes, VoiceFilter::kNumTypes,
                                              SynthEngine::kNumGlideModes, Voice::kNumEnvs,
                                              SynthEngine::kNumPanModes};
int  choices[kNumKnobPages] = {0, 0, SynthEngine::kPoly, VoiceFilter::kLp24,
                               SynthEngine::kGlideOnset, Voice::kAmpEnv, 0};
bool limit[2]  = {};
bool fixed[2]  = {};
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

// Read by the main loop for the LEDs: flash until these times (ms).
volatile uint32_t choice_flash_until = 0;
volatile uint32_t panic_flash_until  = 0;
volatile float    amp_now            = 0.0f;

bool Shift()
{
    return shift_latched || shift_held;
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
int              web_state[kNumParams]; // values in the last state sent

// The audio callback appends to this ring; the main loop reads what's new
// for each frame. A power of two, longer than a frame takes.
constexpr uint32_t  kTraceRing = 64;
float DSY_SDRAM_BSS trace_ring[kTraceRing][kNumTraces];
volatile uint32_t   trace_written = 0; // points so far
uint32_t            trace_read    = 0; // main loop: points sent
int                 trace_samples = 0, trace_period = 96;

volatile float peak        = 0.0f; // out
volatile bool  reset_peaks = false;

Telemetry DSY_SDRAM_BSS telemetry; // the main loop's copy

// LED 2 flashes the new choice if its page is showing.
void SetChoice(int p, int value, uint32_t now_ms)
{
    int n      = kChoiceCounts[p];
    choices[p] = value < 0 ? 0 : (value >= n ? n - 1 : value);
    if(p == page)
        choice_flash_until = now_ms + 1000;
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
        default:
            if(index < kNumKnobParams)
                knobs.Set(index / 2, index % 2, raw / 16383.0f);
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
        default:
            if(index < kNumKnobParams)
                return (int)(knobs.Value(index / 2, index % 2) * 16383.0f + 0.5f);
            return choices[index - kFirstChoice];
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
        point[kTraceVoices] = (float)p.voices;
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
// Notes, pitch bend and a few CCs on any channel, from the TRS input or USB.

void Panic(uint32_t now_ms)
{
    engine.AllNotesOff();
    engine.Sustain(false);
    play_sounding     = -1;
    web_play          = false;
    panic_flash_until = now_ms + 300;
}

void HandleMidi(MidiEvent e, uint32_t now_ms)
{
    switch(e.type)
    {
        case NoteOn:
        {
            NoteOnEvent n = e.AsNoteOn();
            engine.NoteOn(n.note, n.velocity);
            break;
        }
        case NoteOff: engine.NoteOff(e.AsNoteOff().note); break;
        case PitchBend: engine.PitchBend(e.AsPitchBend().value / 8192.0f); break;
        case ControlChange:
        {
            ControlChangeEvent cc = e.AsControlChange();
            if(cc.control_number == 64)
                engine.Sustain(cc.value >= 64);
            else if(cc.control_number == 120 || cc.control_number == 123)
                Panic(now_ms);
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

// The patch, from the knobs and choices.
SynthEngine::Patch MakePatch()
{
    auto k = [](int param) { return knobs.Value(param / 2, param % 2); };

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
    for(int e = 0; e < Voice::kNumEnvs; e++)
        for(int s = 0; s < 4; s++)
            p.env[e][s] = k(kParamWaveAttack + 4 * e + s);
    p.volume       = k(kParamVolume);
    p.amp_velocity = k(kParamAmpVel);
    p.pan          = k(kParamPan);
    p.pan_mode     = choices[6];
    return p;
}

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

    // Turning: the play note while button 1 is held, else the page's choice.
    int inc = hw.encoder.Increment();
    if(inc != 0 && hw.button1.Pressed())
        SetPlayNote(play_note + inc * (Shift() ? 12 : 1));
    else if(inc != 0 && page == kPresetPage)
        presets.Step(inc);
    else if(inc != 0)
        SetChoice(page, choices[page] + inc, now_ms);
    presets.Apply();

    // The knobs work on this page's layer, with Shift's and the envelope's.
    int layer = Layer(page, Shift(), choices[kEnvPage]);
    if(layer != knobs.Page())
        knobs.SetPage(layer, knob);
    knobs.Update(knob);

    engine.SetPatch(MakePatch());

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
    cpu_meter.OnBlockEnd();
}

int main(void)
{
    hw.Init(true); // 480 MHz
    TuneSdramTimings();
    hw.SetAudioBlockSize(4);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    tables.Init(wave_memory);
    engine.Init(&tables, hw.AudioSampleRate(), hw.AudioBlockSize());
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
            Hue((float)choices[p] / kChoiceCounts[p], &r, &g, &b);
        else
            Hue((float)p / kNumKnobPages, &r, &g, &b);
        hw.led2.Set(r, g, b);
        hw.UpdateLeds();

        presets.Poll(now);
        SendWeb(now);
        System::Delay(10);
    }
}

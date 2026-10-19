// Grainfall - granular effects processor for the Daisy Pod (Daisy Seed 3)
//
// Input is recorded continuously into a 10 s SDRAM buffer and replayed as a
// cloud of grains. The two knobs are shared across three pages; press the
// encoder to change page (LED 2 shows which). After a page change a knob does
// nothing until it is moved, so values don't jump.
//
//   Page 1 (LED 2 blue)    Knob 1 - grain size (5 ms - 500 ms)
//                          Knob 2 - density (1 - 100 grains/s)
//   Page 2 (LED 2 magenta) Knob 1 - position, how far back grains start (0 - 2 s)
//                          Knob 2 - spray, random extra offset (0 - 1 s)
//   Page 3 (LED 2 yellow)  Knob 1 - dry/wet mix
//                          Knob 2 - stereo spread
//   Encoder turn       - pitch in semitones (-24 to +24)
//   Encoder long press - MIDI clock sync on/off
//   Button 1           - bypass (LED 1: green = active, red = bypassed)
//   Button 2           - freeze the buffer (LED 1 turns blue while frozen)
//
// MIDI clock sync listens on both the Pod's TRS MIDI input and USB MIDI (the
// Seed's own USB port). While it is on and clock is arriving, grains start on
// a note-division grid locked to the clock (MIDI Start resets it to the
// downbeat), density picks the division (1 bar to 1/128) and position snaps to
// divisions (0 to 1 bar). LED 1 blinks off on each beat. Sync on with no clock
// falls back to free running, and LED 1 flickers until clock arrives.

#include "daisy_pod.h"
#include "daisysp.h"
#include "grain_engine.h"
#include "midi_clock.h"
#include "paged_knobs.h"

using namespace daisy;
using namespace daisysp;

constexpr size_t kBufferLen = 48000 * 10;
float DSY_SDRAM_BSS buffer_l[kBufferLen];
float DSY_SDRAM_BSS buffer_r[kBufferLen];

DaisyPod       hw;
MidiUsbHandler usb_midi;
GrainEngine    grains;
MidiClock      midi_clock;

constexpr int kNumPages = 3;
constexpr int kNumKnobs = 2;

// Raw knob positions (0..1) per page, initial values chosen to sound sensible.
PagedKnobs<kNumPages, kNumKnobs> pages({
    {0.6f, 0.45f}, // size ~120 ms, density ~8/s
    {0.1f, 0.05f}, // position 0.2 s, spray 50 ms
    {0.5f, 0.5f},  // mix 50 %, spread 0.5
});

int  semitones = 0;
bool bypass    = false;
bool freeze    = false;

// Synced divisions in MIDI clock ticks (24 per quarter note), slow to fast for
// density so the knob runs the same way as when free.
const float kDensityDivs[] = {
    96, 48, 24, 12, 8, 6, 4, 3, 2, 1.5f, 0.75f,
    // 1, 1/2, 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T, 1/64, 1/128
};
const float kPositionDivs[] = {
    0, 3, 6, 12, 18, 24, 36, 48, 96,
    // 0, 1/32, 1/16, 1/8, dotted 1/8, 1/4, dotted 1/4, 1/2, 1 bar
};

constexpr uint32_t kLongPressMs = 600;

bool     sync_on         = false;
bool     long_press_done = false; // the current encoder hold toggled sync
bool     was_synced      = false;
int32_t  last_grid       = 0;     // grid step the last grain started on
uint32_t sample_count    = 0;     // running timestamp for the MIDI clock

// Read by the main loop for LED 1.
volatile bool  clock_locked = false;
volatile float beat_phase   = 0.0f; // 0..1 through the current beat

template <size_t N>
float Pick(const float (&table)[N], float x)
{
    size_t i = (size_t)(x * N);
    return table[i < N ? i : N - 1];
}

void HandleMidi(MidiEvent e, uint32_t now)
{
    if(e.type != SystemRealTime)
        return;
    if(e.srt_type == TimingClock)
        midi_clock.Tick(now);
    else if(e.srt_type == Start)
        midi_clock.Start();
}

float ExpMap(float x, float lo, float hi)
{
    return lo * powf(hi / lo, x);
}

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    hw.ProcessAllControls();

    // MIDI arrives in the background; polling once per 4-sample block keeps
    // the tick timestamps within a block of the truth.
    uint32_t now = sample_count;
    hw.midi.Listen();
    usb_midi.Listen();
    while(hw.midi.HasEvents())
        HandleMidi(hw.midi.PopEvent(), now);
    while(usb_midi.HasEvents())
        HandleMidi(usb_midi.PopEvent(), now);

    if(hw.button1.RisingEdge())
        bypass = !bypass;
    if(hw.button2.RisingEdge())
        freeze = !freeze;

    semitones += hw.encoder.Increment();
    semitones = semitones < -24 ? -24 : (semitones > 24 ? 24 : semitones);

    const DaisyPod::Knob knob_ids[kNumKnobs] = {DaisyPod::KNOB_1, DaisyPod::KNOB_2};
    float                knob[kNumKnobs];
    for(int k = 0; k < kNumKnobs; k++)
        knob[k] = hw.GetKnobValue(knob_ids[k]);

    // Long press toggles sync as soon as the hold is long enough; a short
    // press changes page on release.
    if(hw.encoder.Pressed() && hw.encoder.TimeHeldMs() >= kLongPressMs
       && !long_press_done)
    {
        sync_on         = !sync_on;
        long_press_done = true;
    }
    if(hw.encoder.FallingEdge() && long_press_done)
        long_press_done = false;
    else if(hw.encoder.FallingEdge())
        pages.NextPage(knob);
    pages.Update(knob);

    GrainEngine::Params p;
    p.size_s     = ExpMap(pages.Value(0, 0), 0.005f, 0.5f);
    p.density_hz = ExpMap(pages.Value(0, 1), 1.0f, 100.0f);
    p.position_s = pages.Value(1, 0) * 2.0f;
    p.spray_s    = pages.Value(1, 1) * 1.0f;
    p.spread     = pages.Value(2, 1);
    p.pitch      = powf(2.0f, semitones / 12.0f);
    p.freeze     = freeze;

    bool  synced = sync_on && midi_clock.Locked(now);
    float div    = 0.0f;
    if(synced)
    {
        float spt     = midi_clock.SamplesPerTick();
        float sr      = hw.AudioSampleRate();
        div           = Pick(kDensityDivs, pages.Value(0, 1));
        p.ext_trigger = true;
        p.density_hz  = sr / (div * spt);
        p.position_s  = Pick(kPositionDivs, pages.Value(1, 0)) * spt / sr;

        // Entering sync: pick up the grid where it is instead of firing.
        float pos = midi_clock.Position(now);
        if(!was_synced)
            last_grid = (int32_t)floorf(pos / div);
        beat_phase = fmodf(pos, (float)MidiClock::kTicksPerBeat)
                     / MidiClock::kTicksPerBeat;
    }
    was_synced   = synced;
    clock_locked = synced;
    grains.SetParams(p);

    float mix = pages.Value(2, 0);

    for(size_t i = 0; i < size; i++)
    {
        if(synced && midi_clock.AwaitingDownbeat())
        {
            last_grid = -1; // so the downbeat (step 0) starts a grain
        }
        else if(synced)
        {
            int32_t grid = (int32_t)floorf(midi_clock.Position(now + i) / div);
            if(grid != last_grid)
            {
                grains.Trigger();
                last_grid = grid;
            }
        }

        float wet_l, wet_r;
        // Keep the engine running in bypass so the buffer stays current.
        grains.Process(in[0][i], in[1][i], &wet_l, &wet_r);
        if(bypass)
        {
            out[0][i] = in[0][i];
            out[1][i] = in[1][i];
            continue;
        }
        out[0][i] = in[0][i] * (1.0f - mix) + wet_l * mix;
        out[1][i] = in[1][i] * (1.0f - mix) + wet_r * mix;
    }
    sample_count += size;
}

int main(void)
{
    hw.Init();
    hw.SetAudioBlockSize(4);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    grains.Init(hw.AudioSampleRate(), buffer_l, buffer_r, kBufferLen);
    midi_clock.Init(hw.AudioSampleRate());

    MidiUsbHandler::Config usb_cfg;
    usb_cfg.transport_config.periph = MidiUsbTransport::Config::INTERNAL;
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
    };
    while(1)
    {
        if(bypass)
            hw.led1.Set(1.0f, 0.0f, 0.0f);
        else if(freeze)
            hw.led1.Set(0.0f, 0.0f, 1.0f);
        else
            hw.led1.Set(0.0f, 1.0f, 0.0f);
        if(sync_on)
        {
            // Locked: dark for the start of each beat. Waiting: fast flicker.
            bool dark = clock_locked ? beat_phase < 0.15f
                                     : (System::GetNow() / 125) % 2;
            if(dark)
                hw.led1.Set(0.0f, 0.0f, 0.0f);
        }
        const float *c = page_colours[pages.Page()];
        hw.led2.Set(c[0], c[1], c[2]);
        hw.UpdateLeds();
        System::Delay(20);
    }
}

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
//   Encoder turn - pitch in semitones (-24 to +24)
//   Button 1     - bypass (LED 1: green = active, red = bypassed)
//   Button 2     - freeze the buffer (LED 1 turns blue while frozen)

#include "daisy_pod.h"
#include "daisysp.h"
#include "grain_engine.h"

using namespace daisy;
using namespace daisysp;

constexpr size_t kBufferLen = 48000 * 10;
float DSY_SDRAM_BSS buffer_l[kBufferLen];
float DSY_SDRAM_BSS buffer_r[kBufferLen];

DaisyPod    hw;
GrainEngine grains;

constexpr int kNumPages = 3;
constexpr int kNumKnobs = 2;

// Raw knob positions (0..1) per page, initial values chosen to sound sensible.
float page_values[kNumPages][kNumKnobs] = {
    {0.6f, 0.45f}, // size ~120 ms, density ~8/s
    {0.1f, 0.05f}, // position 0.2 s, spray 50 ms
    {0.5f, 0.5f},  // mix 50 %, spread 0.5
};
// The knob reading when the page was entered; a knob takes over once it
// moves away from this.
float knob_at_entry[kNumKnobs];
bool  knob_live[kNumKnobs];

int  page      = 0;
int  semitones = 0;
bool bypass    = false;
bool freeze    = false;

float ExpMap(float x, float lo, float hi)
{
    return lo * powf(hi / lo, x);
}

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    hw.ProcessAllControls();

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

    if(hw.encoder.RisingEdge())
    {
        page = (page + 1) % kNumPages;
        for(int k = 0; k < kNumKnobs; k++)
        {
            knob_at_entry[k] = knob[k];
            knob_live[k]     = false;
        }
    }
    for(int k = 0; k < kNumKnobs; k++)
    {
        if(!knob_live[k] && fabsf(knob[k] - knob_at_entry[k]) > 0.02f)
            knob_live[k] = true;
        if(knob_live[k])
            page_values[page][k] = knob[k];
    }

    GrainEngine::Params p;
    p.size_s     = ExpMap(page_values[0][0], 0.005f, 0.5f);
    p.density_hz = ExpMap(page_values[0][1], 1.0f, 100.0f);
    p.position_s = page_values[1][0] * 2.0f;
    p.spray_s    = page_values[1][1] * 1.0f;
    p.spread     = page_values[2][1];
    p.pitch      = powf(2.0f, semitones / 12.0f);
    p.freeze     = freeze;
    grains.SetParams(p);

    float mix = page_values[2][0];

    for(size_t i = 0; i < size; i++)
    {
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
}

int main(void)
{
    hw.Init();
    hw.SetAudioBlockSize(4);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    grains.Init(hw.AudioSampleRate(), buffer_l, buffer_r, kBufferLen);

    // Every knob starts "not live" so the defaults above hold until a knob moves.
    hw.StartAdc();
    System::Delay(10); // let the ADC settle before taking the reference reading
    hw.ProcessAnalogControls();
    knob_at_entry[0] = hw.GetKnobValue(DaisyPod::KNOB_1);
    knob_at_entry[1] = hw.GetKnobValue(DaisyPod::KNOB_2);

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
        const float *c = page_colours[page];
        hw.led2.Set(c[0], c[1], c[2]);
        hw.UpdateLeds();
        System::Delay(20);
    }
}

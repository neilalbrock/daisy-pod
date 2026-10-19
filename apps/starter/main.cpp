// Starter - toolchain and hardware check for the Daisy Pod (Daisy Seed 3)
//
// Also the template that `make new` copies for a new app.
//
//   Knob 1  - low-pass filter cutoff
//   Knob 2  - output level
//   Button 1 - bypass (LED 1 green = active, red = bypassed)
//   LED 2   - blinks to show the main loop is running

#include "daisy_pod.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

DaisyPod hw;
Svf      filter_l, filter_r;

bool  bypass = false;
float level  = 1.0f;

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    hw.ProcessAllControls();

    if(hw.button1.RisingEdge())
        bypass = !bypass;

    // Exponential mapping so the knob feels musical (20 Hz - 18 kHz)
    float cutoff = 20.0f * powf(900.0f, hw.GetKnobValue(DaisyPod::KNOB_1));
    filter_l.SetFreq(cutoff);
    filter_r.SetFreq(cutoff);
    level = hw.GetKnobValue(DaisyPod::KNOB_2);

    for(size_t i = 0; i < size; i++)
    {
        if(bypass)
        {
            out[0][i] = in[0][i];
            out[1][i] = in[1][i];
            continue;
        }
        filter_l.Process(in[0][i]);
        filter_r.Process(in[1][i]);
        out[0][i] = filter_l.Low() * level;
        out[1][i] = filter_r.Low() * level;
    }
}

int main(void)
{
    hw.Init();
    hw.SetAudioBlockSize(4);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    float sample_rate = hw.AudioSampleRate();

    filter_l.Init(sample_rate);
    filter_r.Init(sample_rate);
    filter_l.SetRes(0.2f);
    filter_r.SetRes(0.2f);

    hw.StartAdc();
    hw.StartAudio(AudioCallback);

    bool heartbeat = false;
    while(1)
    {
        hw.led1.Set(bypass ? 1.0f : 0.0f, bypass ? 0.0f : 1.0f, 0.0f);
        hw.led2.Set(0.0f, 0.0f, heartbeat ? 0.5f : 0.0f);
        hw.UpdateLeds();
        heartbeat = !heartbeat;
        System::Delay(500);
    }
}

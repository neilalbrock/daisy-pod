// USB audio - the Pod as a USB sound card, and a hardware check for
// common/usb_device/ (UsbAudio)
//
// The computer sees a stereo 24-bit, 48 kHz output and input. Audio from the
// computer plays out the line out, and the line in is recorded by the
// computer.
//
// Loopback mode sends the computer's audio straight back to it, bypassing
// the codec, so a recording of what the computer plays should match it
// sample for sample. loopback_check.py compares the two. The line out still
// plays the computer's audio at Knob 1's level, but the line in is ignored.
//
//   Knob 1   - level of the computer's audio at the line out
//   Knob 2   - line in monitoring level at the line out (direct monitoring)
//   Button 1 - loopback mode on/off
//   LED 1    - green while the computer is playing to the Pod, blue while
//              loopback is on (both: cyan)
//   LED 2    - red while the computer is recording from the Pod

#include "daisy_pod.h"
#include "usb_device/usb_audio.h"

using namespace daisy;

DaisyPod hw;
UsbAudio usb;

bool loopback = false;

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    hw.ProcessAllControls();
    if(hw.button1.RisingEdge())
        loopback = !loopback;
    float usb_level     = hw.GetKnobValue(DaisyPod::KNOB_1);
    float monitor_level = hw.GetKnobValue(DaisyPod::KNOB_2);

    usb.Read(out[0], out[1], size);
    if(loopback)
    {
        usb.Write(out[0], out[1], size);
        monitor_level = 0.0f;
    }
    else
    {
        usb.Write(in[0], in[1], size);
    }

    for(size_t i = 0; i < size; i++)
    {
        out[0][i] = out[0][i] * usb_level + in[0][i] * monitor_level;
        out[1][i] = out[1][i] * usb_level + in[1][i] * monitor_level;
    }
}

int main(void)
{
    hw.Init();
    hw.SetAudioBlockSize(4);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    usb.Init("Daisy Pod");

    hw.StartAdc();
    hw.StartAudio(AudioCallback);

    while(1)
    {
        hw.led1.Set(
            0.0f, usb.Playing() ? 1.0f : 0.0f, loopback ? 1.0f : 0.0f);
        hw.led2.Set(usb.Recording() ? 1.0f : 0.0f, 0.0f, 0.0f);
        hw.UpdateLeds();
        System::Delay(20);
    }
}

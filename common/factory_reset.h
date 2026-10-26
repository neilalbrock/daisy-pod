// Factory reset at power-up: both buttons held
//
// Holding both of the Pod's buttons while it powers up (through the
// bootloader's 2.5 s wait) asks for a factory reset, as on the Slöer: the
// app's presets and saved settings go, and everything starts from its
// defaults (Presets::Init does the erasing). Both LEDs flash white until
// the buttons are let go, and at least for 0.6 s; waiting for the release
// keeps the held buttons from acting as presses once audio starts.
//
// Call it after hw.Init(), before Presets::Init() and StartAudio(). Unlike
// the rest of common/, this touches the hardware, so only main.cpp should
// include it.

#pragma once

#include "daisy_pod.h"

inline bool FactoryResetHeld(daisy::DaisyPod &hw)
{
    // The buttons are debounced over a few milliseconds of reads.
    for(int i = 0; i < 20; i++)
    {
        hw.ProcessDigitalControls();
        daisy::System::Delay(1);
    }
    if(!hw.button1.Pressed() || !hw.button2.Pressed())
        return false;

    for(uint32_t n = 0;; n++)
    {
        float lit = (n / 8) % 2 == 0 ? 1.0f : 0.0f;
        hw.led1.Set(lit, lit, lit);
        hw.led2.Set(lit, lit, lit);
        hw.UpdateLeds();
        daisy::System::Delay(10);
        hw.ProcessDigitalControls();
        if(n >= 60 && !hw.button1.Pressed() && !hw.button2.Pressed())
            break;
    }
    hw.led1.Set(0.0f, 0.0f, 0.0f);
    hw.led2.Set(0.0f, 0.0f, 0.0f);
    hw.UpdateLeds();
    return true;
}

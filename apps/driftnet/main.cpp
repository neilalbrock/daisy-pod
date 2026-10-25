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
//   Page 1 (LED 2 blue)    Knob 1 - TIME: delay time
//                          Knob 2 - MODIFY: feedback (holds at max)
//   Page 2 (LED 2 magenta) Knob 1 - LENGTH: shortens the loop, to 1/32
//                          Knob 2 - MODIFY: loop speed, 4x reverse .. 4x forward
//   Page 3 (LED 2 yellow)  Knob 1 - CLOCK: 2k .. 64k in steps of fifths and fourths
//                          Knob 2 - MIX: dry and wet both full at noon
//                          (a limiter holds dry + wet under -1 dBFS)
//   Encoder turn (page 3)  - routing: what the Wet Channel hears while the
//                            loop plays: input, input + loop, loop only.
//                            LED 2 flashes red, green or blue
//   Encoder long press - mono in: the left input feeds both channels
//                        (LED 1 flashes cyan for on, dark for off)
//   Button 1           - Wet Channel on; tap again for off
//   Button 1 hold      - freeze, while held
//   Button 2           - Micro-Looper: play the last moments; tap again to
//                        go back to listening (recording over the loop)
//   Button 2 hold      - overdub, while held
//
// LED 1 is green while the Wet Channel is on and white while frozen, with
// red added while the loop plays. It blinks off at the start of each pass,
// and its red flickers while overdubbing.

#include "daisy_pod.h"
#include "bypass_fade.h"
#include "drift_engine.h"
#include "limiter.h"
#include "paged_knobs.h"
#include "press_hold.h"

using namespace daisy;

constexpr size_t kLoopFrames  = 1 << 17; // 2.7 s at 48k, 65 s at 2k
constexpr size_t kDelayFrames = 1 << 17;
float DSY_SDRAM_BSS loop_buffer[kLoopFrames * 2];
float DSY_SDRAM_BSS delay_buffer[kDelayFrames * 2];

DaisyPod    hw;
DriftEngine engine;
Limiter     limiter; // on dry + wet, so a loud wet signal doesn't clip
BypassFade  bypass_fade;

constexpr int kNumPages = 3;
constexpr int kNumKnobs = 2;

// Raw knob positions (0..1) per page, initial values chosen to sound sensible.
PagedKnobs<kNumPages, kNumKnobs> pages({
    {0.5f, 0.4f},  // time, feedback
    {1.0f, 0.72f}, // full length, 1x forward
    {0.9f, 0.5f},  // clock 48k, mix both full
});

constexpr uint32_t kHoldMs = 400; // a button held this long freezes or overdubs

DriftEngine::Routing routing = DriftEngine::Routing::kIn;
bool                 wet_on  = false;
bool                 freeze  = false;
bool                 mono_in = false;

PressHold encoder_press(600); // hold: mono in
PressHold button1_press(kHoldMs);
PressHold button2_press(kHoldMs);
bool      b1_switched_on = false; // this press turned its channel on, so
bool      b2_switched_on = false; // its release mustn't turn it off again

// Read by the main loop for the LEDs: flash until these times (ms).
volatile uint32_t routing_flash_until = 0;
volatile uint32_t mono_flash_until    = 0;

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    hw.ProcessAllControls();
    uint32_t now_ms = System::GetNow();

    // Button 1: a press switches the Wet Channel on at once; a tap switches
    // it off; holding freezes it until release.
    if(hw.button1.RisingEdge())
    {
        b1_switched_on = !wet_on;
        wet_on         = true;
    }
    if(button1_press.Update(hw.button1.Pressed(), hw.button1.TimeHeldMs())
           == PressHold::Event::kTap
       && !b1_switched_on)
        wet_on = false;
    freeze = hw.button1.Pressed() && hw.button1.TimeHeldMs() >= kHoldMs;

    // Button 2: a press starts the loop at once, so it ends where you
    // pressed; a tap goes back to listening; holding overdubs.
    if(hw.button2.RisingEdge())
    {
        b2_switched_on = !engine.LooperPlaying();
        if(b2_switched_on)
            engine.LooperEngage();
    }
    if(button2_press.Update(hw.button2.Pressed(), hw.button2.TimeHeldMs())
           == PressHold::Event::kTap
       && !b2_switched_on)
        engine.LooperRelease();
    engine.SetOverdub(hw.button2.Pressed() && hw.button2.TimeHeldMs() >= kHoldMs);

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
        pages.NextPage(knob);
    pages.Update(knob);

    int inc = hw.encoder.Increment();
    if(inc != 0 && pages.Page() == 2)
    {
        int r               = (int)routing + inc;
        routing             = (DriftEngine::Routing)(r < 0 ? 0 : (r > 2 ? 2 : r));
        routing_flash_until = now_ms + 1000;
    }

    DriftEngine::Params p;
    p.time        = pages.Value(0, 0);
    p.wet_modify  = pages.Value(0, 1);
    p.length      = pages.Value(1, 0);
    p.loop_modify = pages.Value(1, 1);
    p.clock       = pages.Value(2, 0);
    p.mix         = pages.Value(2, 1);
    p.routing     = routing;
    p.wet_on      = wet_on;
    p.freeze      = freeze;
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
        bypass_fade.Process(bypassed, in_l, in_r, &l, &r);
        out[0][i] = l;
        out[1][i] = r;
    }
}

int main(void)
{
    hw.Init(true); // 480 MHz: the core can tick 1.33 times a sample at 64k
    hw.SetAudioBlockSize(4);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    engine.Init(hw.AudioSampleRate(), loop_buffer, kLoopFrames, delay_buffer, kDelayFrames);
    limiter.Init(hw.AudioSampleRate());
    bypass_fade.Init(hw.AudioSampleRate());

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
    uint32_t loop_starts = 0, dark_until = 0;
    while(1)
    {
        uint32_t now = System::GetNow();

        // LED 1: mono in flash, else the two channels, dark briefly at each
        // pass of the loop.
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
        else if(playing && now < dark_until)
            hw.led1.Set(0.0f, 0.0f, 0.0f);
        else if(freeze)
            hw.led1.Set(1.0f, 1.0f, 1.0f);
        else
        {
            float red = playing ? 1.0f : 0.0f;
            if(engine.Overdubbing() && (now / 80) % 2)
                red = 0.0f;
            hw.led1.Set(red, wet_on ? 1.0f : 0.0f, 0.0f);
        }

        // LED 2: page colour, or the routing position just after a change.
        const float *c = now < routing_flash_until ? position_colours[(int)routing]
                                                   : page_colours[pages.Page()];
        hw.led2.Set(c[0], c[1], c[2]);
        hw.UpdateLeds();

        System::Delay(10);
    }
}

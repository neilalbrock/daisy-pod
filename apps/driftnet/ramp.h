// Driftnet - ramping and bounce
//
// MOOD's ramping, from its left bank of DIP switches: any of TIME, the two
// MODIFYs, CLOCK and LENGTH can move by themselves. With BOUNCE they sweep
// back and forth continuously; without it they ramp once, each time a
// channel is switched on, from the far end of their range to where the knob
// is. SWEEP picks the range: from the bottom up to the knob, or from the
// knob up to the top. POLARITY turns the movement over. RAMP SPEED sets the
// rate, and WAVEFORM the bounce's shape, morphing from triangle through
// square, sine and random to smoothed random.
//
// Expression over MIDI (CC 100) takes over the same knobs, across the same
// range. With no knobs chosen it controls MIX, as on the pedal.
//
// Runs once per block. libDaisy-free.

#pragma once

#include "dsp.h"

class Ramp
{
  public:
    enum Target
    {
        kTime,
        kWetModify,
        kClock,
        kLoopModify,
        kLength,
        kNumTargets,
    };

    struct Settings
    {
        bool  targets[kNumTargets] = {};
        bool  bounce     = false;
        bool  top        = false; // SWEEP: from the knob to the top
        bool  reverse    = false; // POLARITY
        float speed      = 0.5f;  // RAMP SPEED, 0..1
        float shape      = 0.0f;  // WAVEFORM, 0..1
        bool  running    = true;  // CC 52 stops and resumes it
        bool  expression = false; // expression has taken over
        float pedal      = 0.0f;  // the expression value, 0..1
    };

    void Init(float block_rate);

    void Trigger() { ramp_ = 0.0f; } // a channel was switched on: ramp again
    void Update(const Settings &s);  // once per block

    bool Active() const;
    // A knob's value, moved if it's a target.
    float Apply(Target t, float knob) const;
    // Mix, which expression controls when no knob is chosen.
    float ApplyMix(float mix) const;
    // Where the movement is, 0..1 across the range (for the web page).
    float Position() const { return settings_.bounce ? wave_ : ramp_; }

  private:
    float Wave() const;

    Settings settings_;
    float    block_rate_ = 12000.0f;
    float    phase_      = 0.0f; // through the bounce's cycle
    float    wave_       = 0.0f; // its value, 0..1
    float    ramp_       = 1.0f; // through the one-off ramp, 1 = done
    float    rand_prev_ = 0.0f, rand_next_ = 0.5f;
    float    speed_set_ = -1.0f, inc_ = 0.0f; // the phase step for that speed
    dsp::Rng rng_;
};

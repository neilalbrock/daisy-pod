// Driftnet - the CLOCK: a second, variable sample rate
//
// Everything after the dry split runs at the core rate, from 2 kHz to
// 64 kHz, while the codec stays at 48 kHz. Each output sample adds
// rate / 48 kHz to a phase; each time it wraps, the core ticks once on the
// input at that instant, interpolated between the two samples around it.
// There's no anti-alias filter: the aliasing at low rates is the point. The
// core's output is linearly interpolated back up to 48 kHz. Above 48 kHz two
// ticks can fall in one sample.
//
// Buffers count core frames, so a lower clock lengthens loops and delays and
// slows down whatever is already recorded. The knob moves in the pedal's
// steps, alternating fifths and fourths, so those changes stay in harmony.
//
// Header only, libDaisy-free.

#pragma once

#include <cmath>

class ClockDomain
{
  public:
    static constexpr int   kSteps          = 11;
    static constexpr float kRates[kSteps]  = {2000.0f,  3000.0f,  4000.0f,  6000.0f,
                                              8000.0f,  12000.0f, 16000.0f, 24000.0f,
                                              32000.0f, 48000.0f, 64000.0f};
    static constexpr float kMaxRate        = 64000.0f;

    void Init(float sample_rate)
    {
        sample_rate_ = sample_rate;
        SetStep(kSteps - 2); // 48k
    }

    // The CLOCK knob (0..1), once per block. A step only changes once the
    // knob is well past the halfway point between two steps, so a knob
    // sitting on a boundary doesn't flicker between them.
    void SetKnob(float knob)
    {
        float pos = knob * (kSteps - 1);
        if(fabsf(pos - step_) > 0.5f + kHysteresis)
            SetStep((int)(pos + 0.5f));
    }

    void SetStep(int step)
    {
        step_  = step < 0 ? 0 : (step >= kSteps ? kSteps - 1 : step);
        ratio_ = kRates[step_] / sample_rate_;
    }

    int   Step() const { return step_; }
    float Rate() const { return kRates[step_]; }

    // Once per output sample: Begin() with the input, then while NextTick()
    // gives an input, run the core on it and Push() its output. Read the
    // sample with Output().
    void Begin(float in_l, float in_r)
    {
        prev_in_[0] = in_[0];
        prev_in_[1] = in_[1];
        in_[0]      = in_l;
        in_[1]      = in_r;
        phase_ += ratio_;
    }

    bool NextTick(float *l, float *r)
    {
        if(phase_ < 1.0f)
            return false;
        phase_ -= 1.0f;
        // The phase passed 1 this far through the sample.
        float t = 1.0f - phase_ / ratio_;
        *l      = prev_in_[0] + t * (in_[0] - prev_in_[0]);
        *r      = prev_in_[1] + t * (in_[1] - prev_in_[1]);
        return true;
    }

    void Push(float l, float r)
    {
        prev_[0] = last_[0];
        prev_[1] = last_[1];
        last_[0] = l;
        last_[1] = r;
    }

    // Between the last two ticks, a tick behind the input.
    void Output(float *l, float *r) const
    {
        *l = prev_[0] + phase_ * (last_[0] - prev_[0]);
        *r = prev_[1] + phase_ * (last_[1] - prev_[1]);
    }

  private:
    static constexpr float kHysteresis = 0.15f; // of a step

    float sample_rate_ = 48000.0f;
    float ratio_       = 1.0f;
    float phase_       = 0.0f;
    int   step_        = 0;
    float in_[2]       = {}, prev_in_[2] = {}; // this sample and the last
    float prev_[2]     = {}, last_[2] = {};    // the last two ticks' output
};

// Slowdrift - the modulation LFO
//
// The Slöer's five waveshapes, as a delay to modulate (a falling delay
// bends the pitch up):
//
//   Sine   - smooth and even, up and down
//   Warp   - falls fast and rises slowly: quick bends up, like a warped record
//   Sink   - falls slowly and rises fast: quick dips down, like a Bigsby arm
//   Square - jumps between the ends: the jumps chirp
//   Random - a new random level each cycle, sample-and-hold style
//
// Every shape passes a 4 ms slew, so a square's or random's jumps chirp
// rather than click, and changing shape doesn't jump. There are two
// outputs, the second a quarter cycle on, one per half of the plate.
// Header only, libDaisy-free.

#pragma once

#include <cmath>

#include "dsp.h"

class Lfo
{
  public:
    enum Shape
    {
        kSine,
        kWarp,
        kSink,
        kSquare,
        kRandom,
        kNumShapes,
    };

    void SetShape(int shape) { shape_ = shape < 0 ? 0 : (shape >= kNumShapes ? kNumShapes - 1 : shape); }

    // Once per block, with the rate Next() runs at.
    void Set(float hz, float tick_rate)
    {
        inc_ = hz / tick_rate;
        if(tick_rate != tick_rate_)
        {
            slew_      = 1.0f - expf(-1.0f / (0.004f * tick_rate));
            tick_rate_ = tick_rate;
        }
    }

    // Once per tick: both outputs, -1..1.
    void Next(float *a, float *b)
    {
        float prev = phase_;
        phase_     = dsp::WrapPhase(phase_ + inc_);
        // A new random level for each output as its cycle starts.
        if(phase_ < prev)
            held_[0] = rng_.Bi();
        if(prev < 0.75f && phase_ >= 0.75f)
            held_[1] = rng_.Bi();

        float p[2] = {phase_, dsp::WrapPhase(phase_ + 0.25f)};
        for(int k = 0; k < 2; k++)
            out_[k] += slew_ * (Value(p[k], k) - out_[k]);
        *a = out_[0];
        *b = out_[1];
    }

    float Phase() const { return phase_; }
    float Output() const { return out_[0]; }

  private:
    // Falls from 1 to -1 over the first `fall` of the cycle, then rises.
    static float Skewed(float p, float fall)
    {
        // cos(pi t) for t in 0..1
        if(p < fall)
            return dsp::FastSin(0.5f * p / fall + 0.25f);
        return -dsp::FastSin(0.5f * (p - fall) / (1.0f - fall) + 0.25f);
    }

    float Value(float p, int k) const
    {
        switch(shape_)
        {
            case kWarp: return Skewed(p, 0.15f);
            case kSink: return Skewed(p, 0.85f);
            case kSquare: return p < 0.5f ? 1.0f : -1.0f;
            case kRandom: return held_[k];
            case kSine:
            default: return dsp::FastSin(p);
        }
    }

    int      shape_  = kSine;
    float    phase_  = 0.0f, inc_ = 0.0f, slew_ = 1.0f, tick_rate_ = 0.0f;
    float    out_[2] = {}, held_[2] = {};
    dsp::Rng rng_;
};

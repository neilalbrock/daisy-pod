// Stormsea - a slow random wander
//
// A value from -1 to 1 that eases from one random point to the next, each
// move taking a random time between two limits, with no tempo: Tide's
// swells now, and the storm's moods to come. Call Next() at a steady rate
// (once per block). libDaisy-free.

#pragma once

#include <cstdint>

#include "dsp.h"

class Wander
{
  public:
    // Each move takes min_s to max_s seconds; Next() is called rate times a
    // second.
    void Init(float min_s, float max_s, float rate, uint32_t seed)
    {
        rng_.Seed(seed);
        min_   = min_s;
        max_   = max_s;
        rate_  = rate;
        from_  = 0.0f;
        to_    = rng_.Bi();
        phase_ = 0.0f;
        step_  = 1.0f / (rng_.Range(min_, max_) * rate_);
    }

    float Next()
    {
        phase_ += step_;
        if(phase_ >= 1.0f)
        {
            phase_ = 0.0f;
            from_  = to_;
            to_    = rng_.Bi();
            step_  = 1.0f / (rng_.Range(min_, max_) * rate_);
        }
        // Half a cosine from one point to the next: (1 - cos(pi phase)) / 2.
        float ease = 0.5f - 0.5f * dsp::FastSin(0.5f * phase_ + 0.25f);
        value_     = from_ + (to_ - from_) * ease;
        return value_;
    }

    float Value() const { return value_; }

  private:
    dsp::Rng rng_;
    float    min_ = 1.0f, max_ = 1.0f, rate_ = 1.0f;
    float    from_ = 0.0f, to_ = 0.0f, phase_ = 0.0f, step_ = 0.0f, value_ = 0.0f;
};

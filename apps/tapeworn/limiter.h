// Tapeworn - output limiter
//
// Dry plus wet at full volume can reach twice full scale, and the codec
// clips hard. This stereo-linked peak limiter keeps the sum under a -1 dBFS
// ceiling: the gain drops at once to hold any sample under it and recovers
// smoothly, so loud passages get quieter rather than square. Both channels
// share one gain, so the stereo image doesn't shift.
//
// Header only and libDaisy-free, like the rest of the DSP.

#pragma once

#include <cmath>

class Limiter
{
  public:
    void Init(float sample_rate, float release_s = 0.15f)
    {
        release_ = 1.0f - expf(-1.0f / (release_s * sample_rate));
        gain_    = 1.0f;
    }

    void Process(float *l, float *r)
    {
        float peak   = fmaxf(fabsf(*l), fabsf(*r));
        float target = peak > kCeiling ? kCeiling / peak : 1.0f;
        if(target < gain_)
            gain_ = target; // instant: nothing gets past the ceiling
        else
            gain_ += release_ * (target - gain_);
        *l *= gain_;
        *r *= gain_;
    }

  private:
    static constexpr float kCeiling = 0.891f; // -1 dBFS

    float release_ = 0.0f;
    float gain_    = 1.0f;
};

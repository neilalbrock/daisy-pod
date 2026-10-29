// Stormsea - a frequency shifter, for Undertow's sinking
//
// Single sideband: two allpass chains whose outputs stay 90 degrees apart
// across the audio band (Olli Niemitalo's 4 + 4 sections, the first chain
// a sample late) make the signal's analytic pair, and a slowly turning
// phasor moves every partial by the same few tenths of a hertz. Both
// chains pass every frequency at unity, and the result keeps the input's
// power on average whatever the chains' phase error, so inside a loop it
// shifts without adding or losing level, as a pitch shifter's crossfades
// don't. Header only, libDaisy-free.

#pragma once

#include <cmath>

class FreqShift
{
  public:
    // hz: the shift, negative to lower; once per block is fine.
    void Set(float hz, float sample_rate)
    {
        float w = 6.28318530718f * hz / sample_rate;
        step_c_ = cosf(w);
        step_s_ = sinf(w);
        // The phasor drifts off the unit circle a hair a sample: put it
        // back each block.
        float n = 1.0f / sqrtf(c_ * c_ + s_ * s_);
        c_ *= n;
        s_ *= n;
    }

    float Process(float x)
    {
        float i = x, q = x;
        for(int k = 0; k < kStages; k++)
        {
            i = a_[k].Process(i, kA[k]);
            q = b_[k].Process(q, kB[k]);
        }
        float i_late = i_last_;
        i_last_      = i;

        float c = c_ * step_c_ - s_ * step_s_;
        s_      = s_ * step_c_ + c_ * step_s_;
        c_      = c;
        return i_late * c_ + q * s_;
    }

  private:
    static constexpr int kStages = 4;
    // Each section's coefficient, squared.
    static constexpr float kA[kStages] = {0.4794008655888f, 0.8762184935393f, 0.9765975895082f, 0.9974992559356f};
    static constexpr float kB[kStages] = {0.1617584983677f, 0.7330289323415f, 0.9453497003291f, 0.9905991566845f};

    // y[n] = a (x[n] + y[n - 2]) - x[n - 2]
    struct Section
    {
        float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;

        float Process(float x, float a)
        {
            float y = a * (x + y2) - x2;
            x2      = x1;
            x1      = x;
            y2      = y1;
            y1      = y;
            return y;
        }
    };

    Section a_[kStages], b_[kStages];
    float   i_last_ = 0.0f;
    float   c_ = 1.0f, s_ = 0.0f, step_c_ = 1.0f, step_s_ = 0.0f;
};

// Slowdrift - octave shifter, for Light's shimmer and Dark's octave down
//
// Two read heads sweep a short ring, half a window apart, each fading in
// and out (sin squared, so the two always add to one) as its delay wraps
// round the window. To raise the pitch the delay shrinks, to lower it, it
// grows, by (1 - ratio) frames a frame. Smeary on transients, as these
// are, which suits a reverb. Timed in frames, so a lower tick rate
// lengthens the window. Header only, libDaisy-free.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "dsp.h"

class PitchShift
{
  public:
    static constexpr size_t kFloats = 2048; // a power of two, longer than the window

    // mem: kFloats floats, in fast RAM. Init clears it.
    void Init(float *mem, float ratio, float window)
    {
        buf_ = mem;
        for(size_t i = 0; i < kFloats; i++)
            buf_[i] = 0.0f;
        window_ = window < kFloats - 4 ? window : (float)(kFloats - 4);
        inc_    = (1.0f - ratio) / window_;
    }

    float Process(float x)
    {
        buf_[pos_] = x;
        pos_       = (pos_ + 1) & (kFloats - 1);

        phase_ += inc_;
        phase_ -= floorf(phase_); // either way round
        float p_b = dsp::WrapPhase(phase_ + 0.5f);
        // sin^2(pi phase) = (1 - cos(2 pi phase)) / 2
        float g_a = 0.5f - 0.5f * dsp::FastSin(dsp::WrapPhase(phase_ + 0.25f));
        return g_a * Read(phase_) + (1.0f - g_a) * Read(p_b);
    }

  private:
    // At a phase through the window, 1 frame back at the least.
    float Read(float phase) const
    {
        float    d    = 1.0f + phase * window_;
        uint32_t i    = (uint32_t)d;
        float    frac = d - (float)i;
        float    a    = buf_[(pos_ - i) & (kFloats - 1)];
        float    b    = buf_[(pos_ - i - 1) & (kFloats - 1)];
        return a + frac * (b - a);
    }

    float   *buf_   = nullptr;
    uint32_t pos_   = 0; // the next write
    float    phase_ = 0.0f, inc_ = 0.0f, window_ = 1024.0f;
};

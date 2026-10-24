// Grainfall - Hold sampler
//
// Keeps recording the live input into its own short buffer. When engaged it
// freezes the last `length` frames and loops them, crossfading over the
// loop point, and that loop replaces the live input into the effects so
// they go on working on it. Engaging and letting go crossfade too.
// Header only, libDaisy-free.

#pragma once

#include <cmath>
#include <cstdint>

#include "dsp.h"
#include "sample_buffer.h"

class Hold
{
  public:
    // data: frames * 2 floats, frames a power of two and longer than the
    // longest hold plus the loop crossfade.
    void Init(float sample_rate, float *data, size_t frames)
    {
        buffer_.Init(data, frames);
        xfade_     = 0.02f * sample_rate;
        mix_inc_   = 1.0f / (0.01f * sample_rate);
        max_len_   = (float)frames - xfade_ - 4.0f;
        frozen_    = false;
        mix_       = 0.0f;
    }

    // Once per sample. length (frames) is read when Hold engages.
    void Process(bool engaged, float length, float in_l, float in_r,
                 float *out_l, float *out_r)
    {
        if(engaged && !frozen_)
        {
            frozen_ = true;
            len_    = dsp::Clamp(floorf(length), xfade_ * 2.0f, max_len_);
            start_  = buffer_.WritePos() - (uint32_t)len_;
            pos_    = 0;
        }
        if(!frozen_)
            buffer_.Write(in_l, in_r);

        mix_ += engaged ? mix_inc_ : -mix_inc_;
        mix_ = dsp::Clamp(mix_, 0.0f, 1.0f);
        if(!engaged && mix_ <= 0.0f)
            frozen_ = false;

        if(!frozen_)
        {
            *out_l = in_l;
            *out_r = in_r;
            return;
        }

        // In the last xfade frames, blend towards the frames just before the
        // loop start, so wrapping back to the start is seamless.
        float l, r;
        buffer_.Read(start_ + pos_, 0.0f, &l, &r);
        float tail = (float)pos_ - (len_ - xfade_);
        if(tail > 0.0f)
        {
            float t = tail / xfade_;
            float pl, pr;
            buffer_.Read(start_ + pos_ - (uint32_t)len_, 0.0f, &pl, &pr);
            float a = sqrtf(1.0f - t), b = sqrtf(t);
            l       = l * a + pl * b;
            r       = r * a + pr * b;
        }
        if(++pos_ >= (uint32_t)len_)
            pos_ = 0;

        *out_l = in_l + (l - in_l) * mix_;
        *out_r = in_r + (r - in_r) * mix_;
    }

  private:
    SampleBuffer buffer_;
    float        xfade_   = 960.0f;
    float        mix_inc_ = 0.0f;
    float        max_len_ = 0.0f;
    bool         frozen_  = false;
    float        mix_     = 0.0f; // 0 = live, 1 = loop
    float        len_     = 0.0f;
    uint32_t     start_   = 0;
    uint32_t     pos_     = 0;
};

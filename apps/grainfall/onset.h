// Grainfall - note onsets
//
// OnsetDetector follows the input level and flags the start of a note: a
// fast envelope jumping well above a slow one. OnsetLog keeps where in the
// sample buffer the last few onsets landed, for the effects that play back
// recent notes (Strum, Arp, Blocks...). Header only, libDaisy-free.

#pragma once

#include <cmath>
#include <cstdint>

#include "dsp.h"

class OnsetDetector
{
  public:
    void Init(float sample_rate)
    {
        attack_  = dsp::SmoothCoef(0.0005f, sample_rate);
        release_ = dsp::SmoothCoef(0.015f, sample_rate);
        slow_    = dsp::SmoothCoef(0.1f, sample_rate);
        holdoff_ = (int32_t)(0.06f * sample_rate);
    }

    // Once per sample on the (mono) input. True on the sample that starts a
    // note.
    bool Process(float x)
    {
        float a = fabsf(x);
        fast_env_ += (a > fast_env_ ? attack_ : release_) * (a - fast_env_);
        slow_env_ += slow_ * (fast_env_ - slow_env_);
        if(wait_ > 0)
        {
            wait_--;
            return false;
        }
        if(fast_env_ > kThreshold && fast_env_ > slow_env_ * kRatio)
        {
            wait_ = holdoff_;
            return true;
        }
        return false;
    }

    float Envelope() const { return fast_env_; }

  private:
    static constexpr float kThreshold = 0.01f; // -40 dBFS
    static constexpr float kRatio     = 2.0f;  // a 6 dB jump

    float   attack_ = 0.0f, release_ = 0.0f, slow_ = 0.0f;
    float   fast_env_ = 0.0f, slow_env_ = 0.0f;
    int32_t holdoff_ = 0, wait_ = 0;
};

class OnsetLog
{
  public:
    static constexpr int kSize = 8;

    void Add(uint32_t frame)
    {
        next_         = (next_ + 1) % kSize;
        frames_[next_] = frame;
        if(count_ < kSize)
            count_++;
    }

    void Clear() { count_ = 0; }
    int  Count() const { return count_; }

    // 0 is the newest onset, Count() - 1 the oldest.
    uint32_t Get(int i) const { return frames_[(next_ - i + kSize) % kSize]; }

  private:
    uint32_t frames_[kSize] = {};
    int      next_          = 0;
    int      count_         = 0;
};

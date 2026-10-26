// Slowdrift - Rise's swell
//
// A gain on the reverb's input. Each note (an onset) drops it to nothing in
// 5 ms, then it rises again over the swell time, slowly at first (it's
// squared), so the reverb blooms in behind the note. It's the input that
// swells, not the output, so earlier notes' tails ring on. With no onsets
// it settles at 1. Header only, libDaisy-free.

#pragma once

class Swell
{
  public:
    void Init(float sample_rate)
    {
        sample_rate_ = sample_rate;
        fall_        = 1.0f / (0.005f * sample_rate);
        SetTime(1.0f);
    }

    // Once per block.
    void SetTime(float seconds) { rise_ = 1.0f / (seconds * sample_rate_); }

    void Trigger() { falling_ = true; }

    // Once per sample: the gain, 0..1.
    float Process()
    {
        if(falling_)
        {
            level_ -= fall_;
            if(level_ <= 0.0f)
            {
                level_   = 0.0f;
                falling_ = false;
            }
        }
        else if(level_ < 1.0f)
            level_ = level_ + rise_ < 1.0f ? level_ + rise_ : 1.0f;
        return level_ * level_;
    }

  private:
    float sample_rate_ = 48000.0f;
    float fall_ = 0.0f, rise_ = 0.0f;
    float level_   = 1.0f;
    bool  falling_ = false;
};

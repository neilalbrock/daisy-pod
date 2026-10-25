#include "wet_delay.h"

#include <cmath>

#include "dsp.h"

namespace
{
constexpr float kMinFrames   = 48.0f;  // 1 ms at 48k
constexpr float kFadeSeconds = 0.05f;  // between read heads
} // namespace

void WetDelay::Init(float *data, size_t frames)
{
    buffer_.Init(data, frames);
    max_delay_ = (uint32_t)frames - 2;
    target_ = delay_[0] = delay_[1] = max_delay_ / 4;
    SetRate(48000.0f);
}

void WetDelay::SetRate(float core_rate)
{
    fade_inc_ = 1.0f / (kFadeSeconds * core_rate);
}

void WetDelay::SetTime(float time)
{
    // Exponential, from 1 ms to the whole ring (2.7 s at 48k).
    float frames = kMinFrames * powf((float)max_delay_ / kMinFrames, dsp::Clamp(time, 0.0f, 1.0f));
    target_      = (uint32_t)frames;
}

void WetDelay::SetFeedback(float modify)
{
    feedback_ = modify > 0.99f ? 1.0f : dsp::Clamp(modify, 0.0f, 1.0f);
}

void WetDelay::Process(float in_l, float in_r, float *out_l, float *out_r)
{
    // A new time goes to the idle head, then the heads crossfade. Ignore
    // differences too small to hear, so a knob's jitter doesn't keep fading.
    if(!fading_)
    {
        uint32_t d    = delay_[active_];
        uint32_t diff = target_ > d ? target_ - d : d - target_;
        if(diff > 2 && diff * 256 > d)
        {
            delay_[1 - active_] = target_;
            fading_             = true;
        }
    }

    uint32_t     write = buffer_.WritePos();
    const float *a     = buffer_.At(write - delay_[active_]);
    float        l = a[0], r = a[1];
    if(fading_)
    {
        const float *b = buffer_.At(write - delay_[1 - active_]);
        float       x  = active_ == 0 ? fade_ : 1.0f - fade_; // towards the idle head
        l += x * (b[0] - l);
        r += x * (b[1] - r);

        fade_ += active_ == 0 ? fade_inc_ : -fade_inc_;
        if(fade_ >= 1.0f || fade_ <= 0.0f)
        {
            fade_   = dsp::Clamp(fade_, 0.0f, 1.0f);
            active_ = 1 - active_;
            fading_ = false;
        }
    }
    *out_l = l;
    *out_r = r;

    // Repeats pile up at full feedback, so round them off above full scale
    // (nearly linear below it).
    float fb = freeze_ ? 1.0f : feedback_;
    float g  = freeze_ ? 0.0f : 1.0f;
    buffer_.Write(2.0f * dsp::SoftClip(0.5f * (in_l * g + l * fb)),
                  2.0f * dsp::SoftClip(0.5f * (in_r * g + r * fb)));
}

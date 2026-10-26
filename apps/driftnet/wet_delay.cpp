#include "wet_delay.h"

#include <cmath>

#include "dsp.h"

namespace
{
constexpr float kMinFrames   = 48.0f; // 1 ms at 48k
constexpr float kFadeSeconds = 0.05f; // between read heads
constexpr float kBendSeconds = 0.15f; // CLASSIC: how long the head takes to glide
} // namespace

void WetDelay::Init(float *data, size_t frames)
{
    buffer_.Init(data, frames);
    max_delay_ = (uint32_t)frames - 2;
    target_ = delay_[0] = delay_[1] = max_delay_ / 4;
    bend_                           = (float)target_;
    SetRate(48000.0f);
}

void WetDelay::SetRate(float core_rate)
{
    if(core_rate == rate_set_)
        return;
    rate_set_ = core_rate;
    fade_inc_  = 1.0f / (kFadeSeconds * core_rate);
    bend_coef_ = dsp::SmoothCoef(kBendSeconds, core_rate);
}

float WetDelay::TimeFrames(float time) const
{
    // Exponential, from 1 ms to the whole ring (2.7 s at 48k).
    return kMinFrames * powf((float)max_delay_ / kMinFrames, dsp::Clamp(time, 0.0f, 1.0f));
}

void WetDelay::SetTime(float time)
{
    if(time == time_set_)
        return;
    time_set_ = time;
    target_   = (uint32_t)TimeFrames(time);
}

void WetDelay::SetTimeFrames(float frames)
{
    time_set_ = -1.0f; // SetTime() works it out again next time
    target_ = (uint32_t)dsp::Clamp(frames, 1.0f, (float)max_delay_);
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

    float l, r;
    if(classic_)
    {
        // One head gliding to the new time, bending the pitch as it goes.
        bend_ += bend_coef_ * ((float)target_ - bend_);
        HeadAt(bend_, &l, &r);
        delay_[0] = delay_[1] = (uint32_t)bend_;
        fading_               = false;
    }
    else
    {
        bend_ = (float)delay_[active_];
        Head(delay_[active_], &l, &r);
    }
    if(fading_)
    {
        float bl, br;
        Head(delay_[1 - active_], &bl, &br);
        float x = active_ == 0 ? fade_ : 1.0f - fade_; // towards the idle head
        l += x * (bl - l);
        r += x * (br - r);

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
    // SPREAD: each side's echo feeds the other, so repeats ping-pong.
    float fb_l = l + spread_ * (r - l);
    float fb_r = r + spread_ * (l - r);
    buffer_.Write(2.0f * dsp::SoftClip(0.5f * (in_l * g + fb_l * fb)),
                  2.0f * dsp::SoftClip(0.5f * (in_r * g + fb_r * fb)));
    if(fresh_ < buffer_.Frames())
        fresh_++;
}

void WetDelay::HeadAt(float delay, float *l, float *r) const
{
    uint32_t whole = (uint32_t)delay;
    if(whole + 1 > fresh_)
    {
        *l = *r = 0.0f;
        return;
    }
    // Between `whole` and `whole + 1` frames back.
    buffer_.Read(buffer_.WritePos() - whole - 1, 1.0f - (delay - (float)whole), l, r);
}

void WetDelay::Head(uint32_t delay, float *l, float *r) const
{
    if(delay > fresh_)
    {
        *l = *r = 0.0f;
        return;
    }
    const float *f = buffer_.At(buffer_.WritePos() - delay);
    *l             = f[0];
    *r             = f[1];
}

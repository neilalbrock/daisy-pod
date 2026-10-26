#include "wet_slip.h"

#include <cmath>

#include "dsp.h"

namespace
{
constexpr float kMinWindow = 240.0f;   // 5 ms at 48k
constexpr float kMaxWindow = 96000.0f; // 2 s at 48k

// MODIFY's zones: 0..12 are reversed, 2x down to 1x; 13 is 1x forward, at
// noon; 14..25 go up a semitone each, to 2x.
constexpr int   kZones      = 26;
constexpr int   kNeutral    = 13;
constexpr float kHysteresis = 0.15f; // of a zone
constexpr float kClassicEdge = 0.05f; // CLASSIC: each window's fade, of its life
} // namespace

void WetSlip::Init(float *data, size_t frames)
{
    buffer_.Init(data, frames);
    SetTime(0.5f);
    SetModify(0.5f);
    Reset();
}

void WetSlip::SetTime(float time)
{
    if(time == time_set_)
        return;
    time_set_ = time;
    window_ = kMinWindow * powf(kMaxWindow / kMinWindow, dsp::Clamp(time, 0.0f, 1.0f));
}

void WetSlip::SetWindowFrames(float frames)
{
    time_set_ = -1.0f; // SetTime() works it out again next time
    window_ = dsp::Clamp(frames, kMinWindow, kMaxWindow);
}

void WetSlip::SetModify(float modify)
{
    // Equal-width zones either side, with 1x forward a narrow zone at noon.
    if(modify == modify_set_)
        return;
    modify_set_ = modify;
    float m     = dsp::Clamp(modify, 0.0f, 1.0f), x;
    if(m < 0.48f)
        x = m / 0.48f * 13.0f - 0.5f;
    else if(m < 0.52f)
        x = 12.5f + (m - 0.48f) / 0.04f;
    else
        x = 13.5f + (m - 0.52f) / 0.48f * 12.0f;

    // Change zone only once the knob is well past a boundary.
    if(fabsf(x - (float)zone_) > 0.5f + kHysteresis)
    {
        int z = (int)floorf(x + 0.5f);
        zone_ = z < 0 ? 0 : (z >= kZones ? kZones - 1 : z);
    }
    speed_ = zone_ < kNeutral ? -exp2f((float)(kNeutral - 1 - zone_) / 12.0f)
                              : exp2f((float)(zone_ - kNeutral) / 12.0f);
}

void WetSlip::Reset()
{
    fresh_ = 0;
    Start(heads_[0], 0.0f);
    Start(heads_[1], 0.5f * window_);
}

void WetSlip::Start(Head &h, float age)
{
    // As if it had started `age` ago. Forward, it reads up to the anchor by
    // the end of its life; reversed, it reads back from the anchor.
    h.anchor = buffer_.WritePos() - 1 - (uint32_t)age;
    h.speed  = speed_;
    h.life   = window_;
    h.offset = speed_ > 0.0f ? -speed_ * window_ : 0.0f;
    h.age    = age;

    // Either way it reaches back |speed| windows further than what's been
    // recorded by then. Just after a Reset() a head stays silent for its
    // whole life rather than jumping in partway through it.
    h.live = (float)fresh_ >= fabsf(speed_) * window_ + 2.0f;
}

void WetSlip::Process(float in_l, float in_r, float *out_l, float *out_r)
{
    if(!freeze_)
    {
        buffer_.Write(in_l, in_r);
        if(fresh_ < buffer_.Frames())
            fresh_++;
    }
    float l = 0.0f, r = 0.0f, weights = 0.0f;
    for(Head &h : heads_)
    {
        if(h.age >= h.life)
            Start(h, 0.0f);

        float a = h.age / h.life, w;
        if(classic_)
            w = fminf(1.0f, fminf(a, 1.0f - a) / kClassicEdge);
        else
        {
            float s = dsp::FastSin(0.5f * a);
            w       = s * s; // Hann
        }
        if(h.live)
        {
            float    pos   = h.offset + h.speed * h.age; // <= 0, from the anchor
            float    whole = floorf(pos);
            uint32_t frame = h.anchor + (uint32_t)(int32_t)whole;
            float    hl, hr;
            buffer_.Read(frame, classic_ ? 0.0f : pos - whole, &hl, &hr);
            l += w * hl;
            r += w * hr;
        }
        weights += w;
        h.age += 1.0f;
    }

    // Two heads half a window apart sum to 1; this keeps the level steady
    // while a window change has them out of step.
    float norm = 1.0f / fmaxf(weights, 0.5f);
    l *= norm;
    r *= norm;

    // SPREAD: side to side, once every two windows.
    if(spread_ > 0.0f)
    {
        pan_phase_ = dsp::WrapPhase(pan_phase_ + 0.5f / window_);
        float m    = 0.5f * (l + r);
        float p    = spread_ * dsp::FastSin(pan_phase_);
        l          = m * (1.0f - p);
        r          = m * (1.0f + p);
    }
    *out_l = l;
    *out_r = r;
}

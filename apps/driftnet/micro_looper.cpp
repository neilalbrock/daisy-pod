#include "micro_looper.h"

#include <cmath>

#include "dsp.h"

namespace
{
// MODIFY in Tape mode: the speeds, and where each zone starts in MIDI
// values (from MOOD's MIDI manual), so CC 19 lands on the same speeds.
constexpr float kSpeeds[8]    = {-4.0f, -2.0f, -1.0f, -0.5f, 0.5f, 1.0f, 2.0f, 4.0f};
constexpr int   kZoneStart[7] = {5, 24, 43, 62, 85, 98, 120};

constexpr float kFadeSeconds = 0.005f; // engage, release, overdub punch in/out
} // namespace

void MicroLooper::Init(float *data, size_t frames)
{
    buffer_.Init(data, frames);
    loop_len_ = (float)(frames - kSeam);
    window_   = loop_len_;
    SetRate(48000.0f);
}

void MicroLooper::SetRate(float core_rate)
{
    gain_inc_ = 1.0f / (kFadeSeconds * core_rate);
}

void MicroLooper::SetLength(float length)
{
    // Full at max, down to 1/32 of the loop at min.
    window_ = loop_len_ * exp2f(5.0f * (dsp::Clamp(length, 0.0f, 1.0f) - 1.0f));
}

void MicroLooper::SetModify(float modify)
{
    int v    = (int)(dsp::Clamp(modify, 0.0f, 1.0f) * 127.0f + 0.5f);
    int zone = 0;
    while(zone < 7 && v >= kZoneStart[zone])
        zone++;
    speed_ = kSpeeds[zone];
}

void MicroLooper::Engage()
{
    // The ring's newest loop_len_ frames, with the seam just before them.
    start_ = buffer_.WritePos() - (uint32_t)loop_len_;
    pos_   = speed_ < 0.0f ? window_ - 1.0f : 0.0f;
    playing_ = true;
    loop_starts_++;
}

void MicroLooper::Release()
{
    playing_    = false;
    dub_target_ = 0.0f;
}

void MicroLooper::SetOverdub(bool on)
{
    dub_target_ = on && playing_ ? 1.0f : 0.0f;
}

void MicroLooper::Read(float *l, float *r)
{
    float target = playing_ ? 1.0f : 0.0f;
    if(gain_ < target)
        gain_ = fminf(gain_ + gain_inc_, target);
    else if(gain_ > target)
        gain_ = fmaxf(gain_ - gain_inc_, target);
    if(gain_ <= 0.0f)
    {
        *l = *r = 0.0f;
        return;
    }

    // The window can shrink under the play head. It ends where the loop
    // does, so a short one holds the newest audio.
    if(pos_ >= window_)
        pos_ = fmodf(pos_, window_);
    uint32_t first = start_ + (uint32_t)(loop_len_ - window_);

    float    whole = floorf(pos_);
    uint32_t frame = first + (uint32_t)whole;
    buffer_.Read(frame, pos_ - whole, l, r);
    dub_frame_ = frame;

    // Near the end, fade into what comes just before the start, so the
    // wrap lands on the same sound.
    float seam = fminf((float)kSeam, window_ * 0.25f);
    float into = pos_ - (window_ - seam);
    if(into > 0.0f)
    {
        float before = pos_ - window_; // -seam..0
        float bw     = floorf(before);
        float bl, br;
        buffer_.Read(first + (uint32_t)(int32_t)bw, before - bw, &bl, &br);
        float x = into / seam;
        *l += x * (bl - *l);
        *r += x * (br - *r);
    }
    *l *= gain_;
    *r *= gain_;

    pos_ += speed_;
    if(pos_ >= window_)
    {
        pos_ -= window_;
        loop_starts_++;
    }
    else if(pos_ < 0.0f)
    {
        pos_ += window_;
        loop_starts_++;
    }
}

void MicroLooper::Write(float l, float r)
{
    if(!playing_)
    {
        buffer_.Write(l, r);
        return;
    }

    if(dub_ < dub_target_)
        dub_ = fminf(dub_ + gain_inc_, dub_target_);
    else if(dub_ > dub_target_)
        dub_ = fmaxf(dub_ - gain_inc_, dub_target_);

    // Once per frame the play head passes, so slow speeds don't add the
    // same frame twice. Layers pile up, so round them off above full scale
    // (nearly linear below it).
    if(dub_ > 0.0f && dub_frame_ != last_dubbed_)
    {
        float *f     = buffer_.At(dub_frame_);
        f[0]         = 2.0f * dsp::SoftClip(0.5f * (f[0] + l * dub_));
        f[1]         = 2.0f * dsp::SoftClip(0.5f * (f[1] + r * dub_));
        last_dubbed_ = dub_frame_;
    }
}

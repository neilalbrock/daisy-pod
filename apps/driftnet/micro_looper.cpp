#include "micro_looper.h"

#include <cmath>

#include "dsp.h"

namespace
{
// MODIFY in Tape mode: the speeds, and where each zone starts in MIDI
// values (from MOOD's MIDI manual), so CC 19 lands on the same speeds.
constexpr float kSpeeds[8]    = {-4.0f, -2.0f, -1.0f, -0.5f, 0.5f, 1.0f, 2.0f, 4.0f};
constexpr int   kZoneStart[7] = {5, 24, 43, 62, 85, 98, 120};

// MODIFY in Stretch mode: how fast the grains' position moves, at points in
// MIDI values. These are the middles of MOOD's zones (none, 1.5x, 2x and 4x
// stretched, either way, and stalled across 61..80), joined up so the knob
// moves smoothly between them.
constexpr int   kRatePoints        = 11;
constexpr float kRateAt[kRatePoints] = {0.0f, 7.5f, 23.0f, 38.5f, 53.5f, 61.0f,
                                        80.0f, 88.5f, 104.0f, 119.0f, 127.0f};
constexpr float kRate[kRatePoints]   = {-1.0f, -1.0f, -0.667f, -0.5f, -0.25f, 0.0f,
                                        0.0f, 0.25f, 0.5f, 0.667f, 1.0f};

constexpr float kFadeSeconds    = 0.005f; // engage, release, overdub punch in/out, mode change
constexpr float kReleaseSeconds = 0.05f;  // Env's detector

// LENGTH's ranges in core frames (seconds at 48k).
constexpr float kMinSlice = 240.0f, kMaxSlice = 48000.0f; // Env: 5 ms to 1 s
constexpr float kMinGrain = 96.0f, kMaxGrain = 48000.0f;  // Stretch: 2 ms to 1 s
} // namespace

void MicroLooper::Init(float *data, size_t frames)
{
    buffer_.Init(data, frames);
    loop_len_ = (float)(frames - kSeam);
    SetRate(48000.0f);
    SetLength(length_);
    SetModify(modify_);
}

void MicroLooper::SetRate(float core_rate)
{
    gain_inc_    = 1.0f / (kFadeSeconds * core_rate);
    env_release_ = expf(-1.0f / (kReleaseSeconds * core_rate));
}

void MicroLooper::SetLength(float length)
{
    length_ = dsp::Clamp(length, 0.0f, 1.0f);

    // Tape: full at max, down to 1/32 of the loop at min.
    window_ = loop_len_ * exp2f(5.0f * (length_ - 1.0f));

    slice_len_  = kMinSlice * powf(kMaxSlice / kMinSlice, length_);
    grain_size_ = kMinGrain * powf(kMaxGrain / kMinGrain, length_);
}

void MicroLooper::SetModify(float modify)
{
    modify_ = dsp::Clamp(modify, 0.0f, 1.0f);
    float v = modify_ * 127.0f;

    int zone = 0;
    while(zone < 7 && (int)(v + 0.5f) >= kZoneStart[zone])
        zone++;
    speed_ = kSpeeds[zone];

    // Env: the lower the knob, the louder you have to play.
    threshold_ = dsp::DbToGain(-12.0f - 48.0f * modify_);

    int i = 1;
    while(i < kRatePoints - 1 && v > kRateAt[i])
        i++;
    float t = (v - kRateAt[i - 1]) / (kRateAt[i] - kRateAt[i - 1]);
    rate_   = kRate[i - 1] + dsp::Clamp(t, 0.0f, 1.0f) * (kRate[i] - kRate[i - 1]);
    if(rate_ != 0.0f)
        dir_ = rate_ < 0.0f ? -1.0f : 1.0f;
}

void MicroLooper::Engage()
{
    // The ring's newest loop_len_ frames, with the seam just before them.
    start_ = buffer_.WritePos() - (uint32_t)loop_len_;
    mode_  = next_mode_;
    Begin();
    mode_gain_ = 1.0f;
    playing_   = true;
    loop_starts_++;
}

void MicroLooper::Begin()
{
    repeating_ = false;
    switch(mode_)
    {
        case Mode::kTape: pos_ = speed_ < 0.0f ? window_ - 1.0f : 0.0f; break;
        case Mode::kEnv: pos_ = 0.0f; break;
        case Mode::kStretch:
            pos_ = dir_ < 0.0f ? loop_len_ - 1.0f : 0.0f;
            StartGrains();
            break;
    }
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

float MicroLooper::LoopPos() const
{
    return mode_ == Mode::kTape ? loop_len_ - window_ + pos_ : pos_;
}

void MicroLooper::SetLoopPos(float pos)
{
    if(mode_ == Mode::kTape)
    {
        // Into the window, which is the end of the loop.
        pos = fmodf(pos - (loop_len_ - window_), window_);
        if(pos < 0.0f)
            pos += window_;
    }
    pos_ = pos;
}

void MicroLooper::SwitchMode(Mode mode)
{
    // Carry on from the same place in the loop.
    float pos  = LoopPos();
    mode_      = mode;
    repeating_ = false;
    SetLoopPos(pos);
    if(mode_ == Mode::kStretch)
        StartGrains();
}

void MicroLooper::StartGrains()
{
    // The second grain as if it had started half a grain ago, so the two
    // are already in step.
    float half = 0.5f * grain_size_;
    float back = pos_ - rate_ * half;
    if(back < 0.0f)
        back += loop_len_;
    else if(back >= loop_len_)
        back -= loop_len_;
    grains_[0] = {pos_, 0.0f, grain_size_, dir_};
    grains_[1] = {back, half, grain_size_, dir_};
}

uint32_t MicroLooper::ReadSeamless(uint32_t first, float len, float q, float *l, float *r) const
{
    float    whole = floorf(q);
    uint32_t frame = first + (uint32_t)whole;
    buffer_.Read(frame, q - whole, l, r);

    // Near the end, fade into what comes just before the start, so the
    // wrap lands on the same sound.
    float seam = fminf((float)kSeam, len * 0.25f);
    float into = q - (len - seam);
    if(into > 0.0f)
    {
        float before = q - len; // -seam..0
        float bw     = floorf(before);
        float bl, br;
        buffer_.Read(first + (uint32_t)(int32_t)bw, before - bw, &bl, &br);
        float x = into / seam;
        *l += x * (bl - *l);
        *r += x * (br - *r);
    }
    return frame;
}

void MicroLooper::Sense(float l, float r)
{
    float a = fmaxf(fabsf(l), fabsf(r));
    env_    = a > env_ ? a : env_ * env_release_;
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
        if(next_mode_ != mode_)
            SwitchMode(next_mode_);
        *l = *r = 0.0f;
        return;
    }

    // A mode change fades out, switches, then fades back in.
    if(next_mode_ != mode_)
    {
        mode_gain_ -= gain_inc_;
        if(mode_gain_ <= 0.0f)
        {
            mode_gain_ = 0.0f;
            SwitchMode(next_mode_);
        }
    }
    else if(mode_gain_ < 1.0f)
        mode_gain_ = fminf(mode_gain_ + gain_inc_, 1.0f);

    switch(mode_)
    {
        case Mode::kTape: ReadTape(l, r); break;
        case Mode::kEnv: ReadEnv(l, r); break;
        case Mode::kStretch: ReadStretch(l, r); break;
    }
    float g = gain_ * mode_gain_;
    *l *= g;
    *r *= g;
}

void MicroLooper::ReadTape(float *l, float *r)
{
    // The window can shrink under the play head. It ends where the loop
    // does, so a short one holds the newest audio.
    if(pos_ >= window_)
        pos_ = fmodf(pos_, window_);
    uint32_t first = start_ + (uint32_t)(loop_len_ - window_);
    dub_frame_     = ReadSeamless(first, window_, pos_, l, r);

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

void MicroLooper::ReadEnv(float *l, float *r)
{
    // Start or stop repeating only where reading the slice and reading the
    // loop give the same sample: outside the slice's fade and the loop's.
    bool in_loop_seam = pos_ >= loop_len_ - (float)kSeam;
    if(repeating_)
    {
        float seam = fminf((float)kSeam, repeat_len_ * 0.25f);
        if(env_ < threshold_ * 0.5f && pos_ - slice_ < repeat_len_ - seam && !in_loop_seam)
            repeating_ = false;
    }
    else if(env_ > threshold_ && !in_loop_seam)
    {
        // Whole frames, so the slice reads exactly what the loop does.
        float len   = floorf(fminf(slice_len_, loop_len_));
        float slice = floorf(pos_ / len) * len;
        if(slice + len > loop_len_)
            slice = loop_len_ - len;
        float seam = fminf((float)kSeam, len * 0.25f);
        if(pos_ - slice < len - seam)
        {
            repeating_  = true;
            slice_      = slice;
            repeat_len_ = len;
        }
    }

    if(repeating_)
    {
        dub_frame_ = ReadSeamless(start_ + (uint32_t)slice_, repeat_len_, pos_ - slice_, l, r);
        pos_ += 1.0f;
        if(pos_ >= slice_ + repeat_len_)
            pos_ -= repeat_len_;
    }
    else
    {
        dub_frame_ = ReadSeamless(start_, loop_len_, pos_, l, r);
        pos_ += 1.0f;
        if(pos_ >= loop_len_)
        {
            pos_ -= loop_len_;
            loop_starts_++;
        }
    }
}

void MicroLooper::ReadStretch(float *l, float *r)
{
    float sum_l = 0.0f, sum_r = 0.0f, weights = 0.0f, loudest = -1.0f;
    for(Grain &g : grains_)
    {
        if(g.age >= g.size)
            g = {pos_, 0.0f, grain_size_, dir_};

        float q = g.start + g.dir * g.age;
        if(q >= loop_len_)
            q -= loop_len_;
        else if(q < 0.0f)
            q += loop_len_;
        float    gl, gr;
        uint32_t frame = ReadSeamless(start_, loop_len_, q, &gl, &gr);

        float s = dsp::FastSin(0.5f * g.age / g.size);
        float w = s * s; // Hann
        sum_l += w * gl;
        sum_r += w * gr;
        weights += w;
        if(w > loudest)
        {
            loudest    = w;
            dub_frame_ = frame;
        }
        g.age += 1.0f;
    }

    // Two grains half a grain apart sum to 1; this keeps the level steady
    // while a LENGTH change has them out of step.
    float norm = 1.0f / fmaxf(weights, 0.5f);
    *l         = sum_l * norm;
    *r         = sum_r * norm;

    pos_ += rate_;
    if(pos_ >= loop_len_)
    {
        pos_ -= loop_len_;
        loop_starts_++;
    }
    else if(pos_ < 0.0f)
    {
        pos_ += loop_len_;
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

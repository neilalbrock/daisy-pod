#include "wet_reverb.h"

#include <cmath>

#include "dsp.h"

namespace
{
constexpr float kMinSize = 1440.0f;  // 30 ms at 48k
constexpr float kMaxSize = 24000.0f; // 500 ms at 48k

// Where the taps sit, as fractions of the size, and how loud they are. The
// last tap of each side feeds the other side.
constexpr float kTapL[8] = {0.13f, 0.21f, 0.34f, 0.45f, 0.56f, 0.69f, 0.83f, 1.00f};
constexpr float kTapR[8] = {0.10f, 0.18f, 0.29f, 0.41f, 0.53f, 0.64f, 0.77f, 0.95f};
constexpr float kTapGain[8] = {1.0f, 0.93f, 0.86f, 0.79f, 0.72f, 0.65f, 0.58f, 0.51f};
constexpr float kOutGain    = 0.4f;

// SPREAD: where each left tap is scattered to (-1 left, 1 right); the right
// taps go to the mirror image.
constexpr float kScatter[8] = {0.8f, -0.3f, 0.5f, -0.9f, 0.2f, -0.6f, 0.95f, -0.1f};

// CLASSIC: a short comb in the feedback, and how much of it.
constexpr float kRingFrames = 113.0f;
constexpr float kRingGain   = 0.35f;

// Diffuser lengths in frames (Freeverb's allpasses, the right side offset).
constexpr uint32_t kStageLen[2][4] = {{225, 341, 441, 556}, {248, 364, 464, 579}};

constexpr float kLfoInc[2] = {0.31f / 48000.0f, 0.37f / 48000.0f}; // per tick: Hz at 48k
constexpr float kDamp      = 0.6f; // one-pole low-pass in the feedback, per tick
} // namespace

void WetReverb::Init(float *data, size_t frames)
{
    line_   = data;
    frames_ = (uint32_t)frames;
    write_  = 0;
    for(size_t i = 0; i < frames * 2; i++)
        line_[i] = 0.0f;

    // The diffusers' lines follow the main one (both sides' stages, with
    // room for the modulation), cleared once here.
    float *mem = data + 2 * frames;
    for(size_t i = 0; i < kDiffuserFloats; i++)
        mem[i] = 0.0f;
    for(int ch = 0; ch < 2; ch++)
        for(int s = 0; s < kStages; s++)
        {
            stages_[ch][s] = {mem, kStageLen[ch][s], 0, 0};
            mem += kStageLen[ch][s] + 1;
        }

    SetRate(48000.0f);
    SetTime(0.5f);
    SetModify(0.5f);
    size_  = size_target_;
    smear_ = smear_target_;
    Reset();
}

void WetReverb::SetRate(float core_rate)
{
    if(core_rate == rate_set_)
        return;
    rate_set_ = core_rate;
    size_coef_  = dsp::SmoothCoef(0.05f, core_rate);
    smear_coef_ = dsp::SmoothCoef(0.02f, core_rate);
}

void WetReverb::SetTime(float time)
{
    if(time == time_set_)
        return;
    time_set_ = time;
    time         = dsp::Clamp(time, 0.0f, 1.0f);
    size_target_ = kMinSize * powf(kMaxSize / kMinSize, time);
    decay_       = 0.35f + 0.5f * time;
}

void WetReverb::SetModify(float modify)
{
    modify        = dsp::Clamp(modify, 0.0f, 1.0f);
    k_            = 0.75f * modify;
    smear_target_ = fminf(1.0f, 2.0f * modify);
}

void WetReverb::Reset()
{
    fresh_ = 0;
    for(auto &side : stages_)
        for(Allpass &ap : side)
            ap.filled = 0;
    damp_[0] = damp_[1] = 0.0f;
}

float WetReverb::Tap(int ch, float delay) const
{
    uint32_t whole = (uint32_t)delay;
    if(whole + 1 > fresh_)
        return 0.0f;
    // The frames `whole` and `whole + 1` back from the write head.
    uint32_t i = write_ >= whole ? write_ - whole : write_ + frames_ - whole;
    uint32_t j = i == 0 ? frames_ - 1 : i - 1;
    float    a = line_[2 * i + ch];
    float    b = line_[2 * j + ch];
    return a + (delay - (float)whole) * (b - a);
}

float WetReverb::Diffuse(int ch, float x)
{
    if(!classic_)
        lfo_[ch] = dsp::WrapPhase(lfo_[ch] + kLfoInc[ch]);

    float y = x;
    for(int s = 0; s < kStages; s++)
    {
        Allpass &ap   = stages_[ch][s];
        uint32_t size = ap.len + 1;

        // v[n] = y + k v[n-D], out = v[n-D] - k v[n]
        float d;
        if(ap.filled <= ap.len)
            d = 0.0f; // still silence since Reset()
        else if(s == kStages - 1)
        {
            float    delay = (float)ap.len - kModDepth * (1.0f + dsp::FastSin(lfo_[ch]));
            uint32_t whole = (uint32_t)delay;
            float    frac  = delay - (float)whole;
            float    a     = ap.line[(ap.pos + size - whole) % size];
            float    b     = ap.line[(ap.pos + size - whole - 1) % size];
            d              = a + frac * (b - a);
        }
        else
            d = ap.line[(ap.pos + 1) % size]; // len frames ago

        float v         = y + k_ * d;
        ap.line[ap.pos] = v;
        ap.pos          = ap.pos + 1 == size ? 0 : ap.pos + 1;
        if(ap.filled <= ap.len)
            ap.filled++;
        y                = d - k_ * v;
    }
    return x + smear_ * (y - x);
}

void WetReverb::Process(float in_l, float in_r, float *out_l, float *out_r)
{
    size_ += size_coef_ * (size_target_ - size_);
    smear_ += smear_coef_ * ((freeze_ ? 1.0f : smear_target_) - smear_);

    float l = 0.0f, r = 0.0f, last_l = 0.0f, last_r = 0.0f;
    for(int i = 0; i < kTaps; i++)
    {
        last_l = Tap(0, size_ * kTapL[i]);
        last_r = Tap(1, size_ * kTapR[i]);
        if(spread_ > 0.0f)
        {
            // Each tap from its own side towards its scattered place.
            float pl = -1.0f + spread_ * (kScatter[i] + 1.0f);
            float pr = 1.0f - spread_ * (kScatter[i] + 1.0f);
            l += kTapGain[i] * (last_l * fminf(1.0f, 1.0f - pl) + last_r * fminf(1.0f, 1.0f - pr));
            r += kTapGain[i] * (last_l * fminf(1.0f, 1.0f + pl) + last_r * fminf(1.0f, 1.0f + pr));
        }
        else
        {
            l += kTapGain[i] * last_l;
            r += kTapGain[i] * last_r;
        }
    }
    *out_l = l * kOutGain;
    *out_r = r * kOutGain;

    // Each side's last tap feeds the other, darkened a little unless frozen.
    float in_gain = freeze_ ? 0.0f : 1.0f;
    float decay   = freeze_ ? 1.0f : decay_;
    if(freeze_)
    {
        damp_[0] = last_r;
        damp_[1] = last_l;
    }
    else
    {
        damp_[0] += kDamp * (last_r - damp_[0]);
        damp_[1] += kDamp * (last_l - damp_[1]);
    }
    float x_l = in_l * in_gain + decay * damp_[0];
    float x_r = in_r * in_gain + decay * damp_[1];
    // The comb and the feedback together must stay under unity gain.
    float ring = classic_ ? fminf(kRingGain, 0.97f - decay) : 0.0f;
    if(ring > 0.0f)
    {
        x_l += ring * Tap(0, kRingFrames);
        x_r += ring * Tap(1, kRingFrames);
    }
    x_l = Diffuse(0, x_l);
    x_r = Diffuse(1, x_r);

    // Round off above full scale, so a frozen wash can't run away.
    line_[2 * write_]     = 2.0f * dsp::SoftClip(0.5f * x_l);
    line_[2 * write_ + 1] = 2.0f * dsp::SoftClip(0.5f * x_r);
    write_                = write_ + 1 == frames_ ? 0 : write_ + 1;
    if(fresh_ < frames_)
        fresh_++;
}

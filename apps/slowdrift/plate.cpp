#include "plate.h"

#include <cmath>

#include "dsp.h"

using namespace dsp;

namespace
{
constexpr float kScale = 48000.0f / 29761.0f; // the paper's lengths are at 29.761 kHz

// The paper's lengths and gains.
constexpr float kInDiff[4]  = {142.0f, 107.0f, 379.0f, 277.0f};
constexpr float kInGain[4]  = {0.75f, 0.75f, 0.625f, 0.625f};
constexpr float kMod[2]     = {672.0f, 908.0f};
constexpr float kDelay1[2]  = {4453.0f, 4217.0f};
constexpr float kAp[2]      = {1800.0f, 2656.0f};
constexpr float kDelay2[2]  = {3720.0f, 3163.0f};
constexpr float kDiffusion1 = 0.7f;
constexpr float kGain       = 0.6f; // output level

// Output taps, as in the paper: which tank line, how far back, which way.
enum TankLine
{
    kDelay1L,
    kApL,
    kDelay2L,
    kDelay1R,
    kApR,
    kDelay2R,
};
struct Tap
{
    TankLine line;
    float    back;
    float    sign;
};
constexpr Tap kTaps[2][7] = {
    {{kDelay1R, 266, 1}, {kDelay1R, 2974, 1}, {kApR, 1913, -1}, {kDelay2R, 1996, 1},
     {kDelay1L, 1990, -1}, {kApL, 187, -1}, {kDelay2L, 1066, -1}},
    {{kDelay1L, 353, 1}, {kDelay1L, 3627, 1}, {kApL, 1228, -1}, {kDelay2L, 2673, 1},
     {kDelay1R, 2111, -1}, {kApR, 335, -1}, {kDelay2R, 121, -1}},
};

// Room for a line at its longest, and a frame for interpolation.
constexpr uint32_t Cap(float frames)
{
    return (uint32_t)frames + 3;
}

constexpr size_t TotalFloats()
{
    size_t n = 2 * Cap(Plate::kMaxPredelay);
    for(int i = 0; i < 4; i++)
        n += 2 * Cap(kInDiff[i] * kScale);
    for(int k = 0; k < 2; k++)
        n += Cap(kMod[k] * kScale + Plate::kMaxExcursion) + Cap(kDelay1[k] * kScale)
             + Cap(kAp[k] * kScale) + Cap(kDelay2[k] * kScale);
    return n;
}
static_assert(TotalFloats() <= Plate::kFloats, "Plate::kFloats is too small");
} // namespace

float Plate::Line::ReadFrac(float d) const
{
    uint32_t i = (uint32_t)d;
    float    a = Read(i), b = Read(i + 1);
    return a + (d - (float)i) * (b - a);
}

float *Plate::Take(Line &line, uint32_t cap, float *mem)
{
    line.buf = mem;
    line.cap = cap;
    line.pos = 0;
    return mem + cap;
}

void Plate::Init(float *mem, float *shifter_mem)
{
    for(size_t i = 0; i < kFloats; i++)
        mem[i] = 0.0f;
    up_.Init(shifter_mem, 2.0f, 1024.0f);

    for(int s = 0; s < 2; s++)
    {
        mem = Take(pre_[s], Cap(kMaxPredelay), mem);
        for(int i = 0; i < 4; i++)
            mem = Take(in_[s][i], Cap(kInDiff[i] * kScale), mem);
    }
    for(int i = 0; i < 4; i++)
        in_len_[i] = (uint32_t)(kInDiff[i] * kScale);

    loop_ = 0.0f;
    for(int k = 0; k < 2; k++)
    {
        mem = Take(mod_[k], Cap(kMod[k] * kScale + kMaxExcursion), mem);
        mem = Take(delay1_[k], Cap(kDelay1[k] * kScale), mem);
        mem = Take(ap_[k], Cap(kAp[k] * kScale), mem);
        mem = Take(delay2_[k], Cap(kDelay2[k] * kScale), mem);

        mod_len_[k]    = kMod[k] * kScale;
        delay1_len_[k] = (uint32_t)(kDelay1[k] * kScale);
        ap_len_[k]     = (uint32_t)(kAp[k] * kScale);
        delay2_len_[k] = (uint32_t)(kDelay2[k] * kScale);
        loop_ += mod_len_[k] + (float)(delay1_len_[k] + ap_len_[k] + delay2_len_[k]);
    }
    for(int side = 0; side < 2; side++)
        for(int j = 0; j < 7; j++)
            taps_[side][j] = (uint32_t)(kTaps[side][j].back * kScale);

    SetDiffusion(1.0f);
}

float Plate::DecayForRt60(float frames) const
{
    // A trip round the whole tank passes the decay gain four times.
    return fminf(powf(0.001f, loop_ / (4.0f * fmaxf(frames, 1.0f))), 1.0f);
}

void Plate::SetDiffusion(float diffusion)
{
    diffusion_ = Clamp(diffusion, 0.0f, 1.0f);
    for(int i = 0; i < 4; i++)
        in_gain_[i] = kInGain[i] * diffusion_;
    diffusion1_ = kDiffusion1 * diffusion_;
}

void Plate::SetPredelay(float frames)
{
    predelay_ = Clamp(frames, 1.0f, kMaxPredelay);
}

void Plate::SetExcursion(float frames)
{
    excursion_ = Clamp(frames, 0.0f, kMaxExcursion);
}

float Plate::Allpass(Line &line, uint32_t len, float g, float x)
{
    float delayed = line.Read(len);
    float w       = x - g * delayed;
    line.Write(w);
    return delayed + g * w;
}

float Plate::Diffuse(Line *lines, float x)
{
    for(int i = 0; i < 4; i++)
        x = Allpass(lines[i], in_len_[i], in_gain_[i], x);
    return x;
}

void Plate::Process(float mid, float side, float lfo0, float lfo1, float *l, float *r)
{
    // Input: predelay, bandwidth, diffusers, for mid and side.
    float in[2] = {mid, side};
    for(int s = 0; s < 2; s++)
    {
        float x = pre_[s].ReadFrac(predelay_);
        pre_[s].Write(in[s]);
        bw_lp_[s] += bandwidth_ * (x - bw_lp_[s]);
        in[s] = Diffuse(in_[s], bw_lp_[s]);
    }
    float half_in[2] = {in[0] + in_side_ * in[1], in[0] - in_side_ * in[1]};

    // As in the paper, the decay diffusion follows the decay, so short
    // decays don't ring.
    float diffusion2 = Clamp(decay_ + 0.15f, 0.25f, 0.5f) * diffusion_;

    // The tank: each half takes the input and the other half's output, the
    // first with the shimmer's share an octave up. The shifter runs even
    // when unheard, so it's ready.
    float up       = up_.Process(cross_[1]);
    float cross[2] = {cross_[1] + shimmer_ * (up - cross_[1]), cross_[0]};
    float lfo[2]   = {lfo0, lfo1};
    float out[2];
    for(int k = 0; k < 2; k++)
    {
        float x       = half_in[k] + cross[k];
        float delayed = mod_[k].ReadFrac(mod_len_[k] + excursion_ * (0.5f + 0.5f * lfo[k]));
        float w       = x + diffusion1_ * delayed;
        mod_[k].Write(w);
        float a = delayed - diffusion1_ * w;

        float t = delay1_[k].Read(delay1_len_[k]);
        delay1_[k].Write(a);
        damp_lp_[k] += (1.0f - damping_) * (t - damp_lp_[k]);
        t = Allpass(ap_[k], ap_len_[k], diffusion2, damp_lp_[k] * decay_);

        float e = delay2_[k].Read(delay2_len_[k]);
        delay2_[k].Write(t);
        out[k] = e * decay_;
    }
    cross_[0] = out[0];
    cross_[1] = out[1];

    // Output taps.
    const Line *lines[6] = {&delay1_[0], &ap_[0], &delay2_[0], &delay1_[1], &ap_[1], &delay2_[1]};
    float       wet[2];
    for(int s = 0; s < 2; s++)
    {
        float sum = 0.0f;
        for(int j = 0; j < 7; j++)
            sum += kTaps[s][j].sign * lines[kTaps[s][j].line]->Read(taps_[s][j]);
        wet[s] = sum * kGain;
    }
    *l = wet[0];
    *r = wet[1];
}

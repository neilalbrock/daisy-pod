#include "space.h"

#include <cmath>

#include "dsp.h"

using namespace dsp;

namespace
{
constexpr float kRate  = 48000.0f;
constexpr float kScale = kRate / 29761.0f; // the paper's lengths are at 29.761 kHz

// The paper's lengths. The input diffusers keep theirs; the tank's scale
// with the mode's size.
constexpr float kInDiff[4]    = {142.0f, 107.0f, 379.0f, 277.0f};
constexpr float kInGain[4]    = {0.75f, 0.75f, 0.625f, 0.625f};
constexpr float kMod[2]       = {672.0f, 908.0f};
constexpr float kExcursion    = 16.0f; // the modulated allpasses' swing
constexpr float kDelay1[2]    = {4453.0f, 4217.0f};
constexpr float kAp[2]        = {1800.0f, 2656.0f};
constexpr float kDelay2[2]    = {3720.0f, 3163.0f};
constexpr float kDiffusion1   = 0.7f;
constexpr float kMaxPredelayS = 0.06f;
constexpr float kLfoHz        = 0.7f;
constexpr float kFadeStep     = 1.0f / (0.01f * kRate); // 10 ms each way
constexpr uint32_t kClearStep = 512; // floats emptied a sample for a mode change

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

struct ModeDef
{
    float size;      // the tank's lengths, as a fraction of the paper's
    float rt60;      // seconds
    float damping;   // in the tank: 0 bright .. 1 dark
    float bandwidth; // at the input: 1 open .. 0 dark
    float predelay;  // seconds
    float mod;       // modulation, as a fraction of the paper's
    float gain;      // output level
};
constexpr ModeDef kModes[Space::kNumModes] = {
    {0.45f, 0.8f, 0.1f, 0.9995f, 0.004f, 0.3f, 0.6f}, // A Bright Room
    {0.65f, 1.8f, 0.6f, 0.45f, 0.012f, 0.5f, 0.8f},   // B Dark Medium
    {0.9f, 3.5f, 0.35f, 0.75f, 0.025f, 0.6f, 0.65f},  // C Large Hall
    {1.0f, 9.0f, 0.45f, 0.65f, 0.06f, 1.0f, 0.6f},    // D Ambient
};

// Room for a line at its longest, and a frame for interpolation.
constexpr uint32_t Cap(float frames)
{
    return (uint32_t)frames + 3;
}

constexpr size_t TotalFloats()
{
    size_t n = Cap(kMaxPredelayS * kRate);
    for(int i = 0; i < 4; i++)
        n += Cap(kInDiff[i] * kScale);
    for(int k = 0; k < 2; k++)
        n += Cap((kMod[k] + kExcursion) * kScale) + Cap(kDelay1[k] * kScale)
             + Cap(kAp[k] * kScale) + Cap(kDelay2[k] * kScale);
    return n;
}
static_assert(TotalFloats() <= Space::kFloats, "Space::kFloats is too small");
} // namespace

float Space::Line::ReadFrac(float d) const
{
    uint32_t i = (uint32_t)d;
    float    a = Read(i), b = Read(i + 1);
    return a + (d - (float)i) * (b - a);
}

float *Space::Take(Line &line, uint32_t cap, float *mem)
{
    line.buf = mem;
    line.cap = cap;
    line.pos = 0;
    return mem + cap;
}

void Space::Init(float *mem)
{
    for(size_t i = 0; i < kFloats; i++)
        mem[i] = 0.0f;
    mem_ = mem;

    mem = Take(pre_, Cap(kMaxPredelayS * kRate), mem);
    for(int i = 0; i < 4; i++)
    {
        mem        = Take(in_[i], Cap(kInDiff[i] * kScale), mem);
        in_len_[i] = (uint32_t)(kInDiff[i] * kScale);
    }
    for(int k = 0; k < 2; k++)
    {
        mem = Take(mod_[k], Cap((kMod[k] + kExcursion) * kScale), mem);
        mem = Take(delay1_[k], Cap(kDelay1[k] * kScale), mem);
        mem = Take(ap_[k], Cap(kAp[k] * kScale), mem);
        mem = Take(delay2_[k], Cap(kDelay2[k] * kScale), mem);
    }

    mode_  = -1;
    fade_  = 0.0f;
    clear_ = kFloats; // already empty
}

void Space::SetMix(float space)
{
    mix_target_ = Clamp(space, 0.0f, 1.0f);
}

void Space::SetMode(int mode)
{
    target_mode_ = mode < 0 ? 0 : (mode >= kNumModes ? kNumModes - 1 : mode);
}

void Space::Apply(int mode)
{
    const ModeDef &m = kModes[mode];
    float          s = kScale * m.size;

    pre_len_ = (uint32_t)(Clamp(m.predelay, 0.0f, kMaxPredelayS) * kRate);
    if(pre_len_ < 1)
        pre_len_ = 1;
    float loop = 0.0f;
    for(int k = 0; k < 2; k++)
    {
        mod_len_[k]    = kMod[k] * s;
        delay1_len_[k] = (uint32_t)(kDelay1[k] * s);
        ap_len_[k]     = (uint32_t)(kAp[k] * s);
        delay2_len_[k] = (uint32_t)(kDelay2[k] * s);
        loop += mod_len_[k] + (float)(delay1_len_[k] + ap_len_[k] + delay2_len_[k]);
    }
    for(int side = 0; side < 2; side++)
        for(int j = 0; j < 7; j++)
        {
            uint32_t back  = (uint32_t)(kTaps[side][j].back * s);
            taps_[side][j] = back < 1 ? 1 : back;
        }

    // A trip round the whole tank passes the decay gain four times.
    decay_      = fminf(powf(0.001f, loop / (4.0f * m.rt60 * kRate)), 0.98f);
    diffusion2_ = Clamp(decay_ + 0.15f, 0.25f, 0.5f);
    damping_    = m.damping;
    bandwidth_  = m.bandwidth;
    excursion_  = kExcursion * kScale * m.mod;
    gain_       = m.gain;
    mode_       = mode;
}

float Space::Allpass(Line &line, uint32_t len, float g, float x)
{
    float delayed = line.Read(len);
    float w       = x - g * delayed;
    line.Write(w);
    return delayed + g * w;
}

void Space::Process(float *l, float *r)
{
    mix_ += 0.001f * (mix_target_ - mix_); // about 20 ms
    float dry_gain = FastSin(0.25f + 0.25f * mix_); // cos(mix * pi / 2)
    float wet_gain = FastSin(0.25f * mix_);         // sin(mix * pi / 2)

    // A mode change: fade out, empty the lines a step at a time while only
    // the dry is heard, switch lengths and fade back in. A volatile pointer,
    // so the compiler can't make the loop a memset (newlib's goes a byte at
    // a time).
    if(target_mode_ != mode_)
    {
        if(fade_ > 0.0f)
            fade_ = fmaxf(fade_ - kFadeStep, 0.0f);
        else
        {
            volatile float *p   = mem_ + clear_;
            uint32_t        end = clear_ + kClearStep < kFloats ? clear_ + kClearStep : kFloats;
            for(uint32_t i = clear_; i < end; i++)
                *p++ = 0.0f;
            clear_ = end;
            if(clear_ == kFloats)
            {
                bw_lp_      = 0.0f;
                damp_lp_[0] = damp_lp_[1] = 0.0f;
                cross_[0] = cross_[1] = 0.0f;
                Apply(target_mode_);
                clear_ = 0;
            }
            *l *= dry_gain;
            *r *= dry_gain;
            return;
        }
    }
    else if(fade_ < 1.0f)
        fade_ = fminf(fade_ + kFadeStep, 1.0f);

    // Input: predelay, bandwidth, diffusers.
    float x = pre_.Read(pre_len_);
    pre_.Write(0.5f * (*l + *r));
    bw_lp_ += bandwidth_ * (x - bw_lp_);
    x = bw_lp_;
    for(int i = 0; i < 4; i++)
        x = Allpass(in_[i], in_len_[i], kInGain[i], x);

    // The tank: each half takes the input and the other half's output.
    lfo_phase_   = WrapPhase(lfo_phase_ + kLfoHz / kRate);
    float lfo[2] = {FastSin(lfo_phase_), FastSin(WrapPhase(lfo_phase_ + 0.25f))};
    float out[2];
    for(int k = 0; k < 2; k++)
    {
        float in      = x + cross_[1 - k];
        float delayed = mod_[k].ReadFrac(mod_len_[k] + excursion_ * (0.5f + 0.5f * lfo[k]));
        float w       = in + kDiffusion1 * delayed;
        mod_[k].Write(w);
        float a = delayed - kDiffusion1 * w;

        float t = delay1_[k].Read(delay1_len_[k]);
        delay1_[k].Write(a);
        damp_lp_[k] += (1.0f - damping_) * (t - damp_lp_[k]);
        t = Allpass(ap_[k], ap_len_[k], diffusion2_, damp_lp_[k] * decay_);

        float e = delay2_[k].Read(delay2_len_[k]);
        delay2_[k].Write(t);
        out[k] = e * decay_;
    }
    cross_[0] = out[0];
    cross_[1] = out[1];

    // Output taps.
    const Line *lines[6] = {&delay1_[0], &ap_[0], &delay2_[0], &delay1_[1], &ap_[1], &delay2_[1]};
    float       wet[2];
    for(int side = 0; side < 2; side++)
    {
        float sum = 0.0f;
        for(int j = 0; j < 7; j++)
            sum += kTaps[side][j].sign * lines[kTaps[side][j].line]->Read(taps_[side][j]);
        wet[side] = sum * gain_ * fade_;
    }

    *l = *l * dry_gain + wet[0] * wet_gain;
    *r = *r * dry_gain + wet[1] * wet_gain;
}

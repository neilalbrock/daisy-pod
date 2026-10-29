#include "sea.h"

#include <cmath>

#include "dsp.h"

using namespace dsp;

namespace
{
// Which way each line takes the input, and gives to each output side: four
// pluses and four minuses each, every pair orthogonal, so mid and side
// (and left and right) go in and come out uncorrelated.
constexpr float kInMid[Sea::kLines]  = {1, 1, -1, 1, -1, -1, 1, -1};
constexpr float kInSide[Sea::kLines] = {1, -1, 1, 1, -1, 1, -1, -1};
constexpr float kOutL[Sea::kLines]   = {1, -1, -1, 1, 1, 1, -1, -1};
constexpr float kOutR[Sea::kLines]   = {1, 1, 1, -1, -1, 1, -1, -1};

constexpr float kInGain  = 0.35f;
constexpr float kOutGain = 0.35355339f; // 1 / sqrt(8)
constexpr float kMatrix  = 0.35355339f; // the Hadamard matrix's scale, 1 / sqrt(8)

// Each line's swing: its own slow rate, so the lines never move together.
constexpr float kSwingHz[Sea::kLines] = {0.13f, 0.17f, 0.23f, 0.29f, 0.37f, 0.43f, 0.53f, 0.61f};

constexpr float kDiffusion[Sea::kDiffusers] = {0.7f, 0.7f, 0.6f, 0.6f};

// The DC block: its corner (about 4 Hz), and its gain, scaled so it never
// passes unity (unscaled, it's 2 / (1 + pole) at Nyquist, and a frozen sea
// creeps up by that every trip).
constexpr float kDcPole = 0.9995f;
constexpr float kDcGain = 0.5f * (1.0f + kDcPole);

// The sea wall: the lines' mean square it holds the sea under (each line
// about -15 dBFS RMS), and how hard it pushes back: each trip round, the
// gain is (ceiling / level) to this power.
constexpr float kCeiling   = 0.03f;
constexpr float kWallSlope = 0.1f;

// Surge's clipper: the most a line can carry.
constexpr float kClip    = 0.3f;
constexpr float kInvClip = 1.0f / kClip;

// Wind's moves, at speed 0 and 1, in seconds; each takes from 60% to 140%
// of that, at random.
constexpr float kCalmMove  = 1.5f;
constexpr float kStormMove = 0.1f;

constexpr float kLn1000 = 6.9077553f; // 60 dB
} // namespace

void Sea::Init(float sample_rate, size_t block_size, float *mem)
{
    sample_rate_ = sample_rate;
    for(int i = 0; i < kLines; i++)
    {
        Line &ln = lines_[i];
        ln.buf   = mem;
        ln.size  = kLengths[i] + kSpare;
        ln.pos   = 0;
        ln.delay = (float)kLengths[i];
        ln.b     = 1.0f;
        ln.a = ln.z = ln.dc_x = ln.dc_y = ln.ap_y = 0.0f;
        ln.phase = (float)i / kLines;
        ln.inc   = kSwingHz[i] / sample_rate;
        ln.wind = ln.wind_step = 0.0f;
        ln.turbulence.Init(kCalmMove, kCalmMove, sample_rate / (float)block_size, 0x5EA0u + 977u * i);
        for(int k = 0; k < ln.size; k++)
            ln.buf[k] = 0.0f;
        mem += ln.size;
    }
    for(int c = 0; c < 2; c++)
        for(int d = 0; d < kDiffusers; d++)
        {
            Diffuser &df = diff_[c][d];
            df.buf       = mem;
            df.size      = kDiffuserLengths[c][d];
            df.pos       = 0;
            df.gain      = kDiffusion[d];
            for(int k = 0; k < df.size; k++)
                df.buf[k] = 0.0f;
            mem += df.size;
        }

    float block_rate = sample_rate / (float)block_size;
    block_frames_    = (float)block_size;
    level_attack_    = 1.0f - expf(-1.0f / (0.02f * block_rate));
    level_release_   = 1.0f - expf(-1.0f / (0.3f * block_rate));
    block_scale_     = 1.0f / ((float)block_size * kLines);

    SetSize(1.0f);
    SetDecay(2.0f, 0.5f);
}

void Sea::SetSize(float size)
{
    size_ = Clamp(size, kMinSize, 1.0f);
    for(int i = 0; i < kLines; i++)
        lines_[i].delay = (float)kLengths[i] * size_;
}

void Sea::SetDecay(float rt60, float hf_ratio)
{
    // The damping only changes when something moved: it takes two expf a
    // line.
    if(fabsf(rt60 - set_rt60_) <= 1e-4f * set_rt60_ && fabsf(hf_ratio - set_hf_ratio_) <= 1e-4f
       && fabsf(size_ - set_size_) <= 1e-4f)
        return;
    set_rt60_     = rt60;
    set_hf_ratio_ = hf_ratio;
    set_size_     = size_;

    // Per line: the gain that dies away by 60 dB in rt60 at DC, and in
    // rt60 x hf_ratio at Nyquist. A one-pole low-pass b / (1 - a z^-1) has
    // gain b / (1 - a) at DC and b / (1 + a) at Nyquist.
    float per_frame = -kLn1000 / (fmaxf(rt60, 0.01f) * sample_rate_);
    float hf        = Clamp(hf_ratio, 0.01f, 1.0f);
    for(int i = 0; i < kLines; i++)
    {
        Line &ln = lines_[i];
        float g  = expf(per_frame * ln.delay);
        float r  = expf(per_frame * ln.delay * (1.0f / hf - 1.0f)); // Nyquist's gain over DC's
        ln.a     = (1.0f - r) / (1.0f + r);
        ln.b     = g * (1.0f - ln.a);
    }
}

void Sea::SetDepth(float frames)
{
    depth_ = Clamp(frames, 0.0f, 0.25f * kMaxMod);
}

void Sea::SetTurbulence(float frames, float speed)
{
    frames     = Clamp(frames, 0.0f, 0.25f * kMaxMod);
    float move = kCalmMove * powf(kStormMove / kCalmMove, Clamp(speed, 0.0f, 1.0f));
    for(int i = 0; i < kLines; i++)
    {
        Line &ln = lines_[i];
        ln.turbulence.SetTimes(0.6f * move, 1.4f * move);
        float target = frames * (1.0f + ln.turbulence.Next());
        ln.wind_step = (target - ln.wind) / block_frames_;
    }
}

void Sea::SetSurge(float surge)
{
    surge_      = Clamp(surge, 0.0f, 1.0f);
    surge_gain_ = 1.0f + kSurgeGain * surge_;
}

void Sea::UpdateWall()
{
    float ms = energy_ * block_scale_;
    energy_  = 0.0f;
    level_ += (ms > level_ ? level_attack_ : level_release_) * (ms - level_);
    wall_ = level_ > kCeiling ? FastExp2(kWallSlope * log2f(kCeiling / level_)) : 1.0f;
}

float Sea::Read(Line &ln, float delay)
{
    // A whole number of frames, then the allpass for the rest (0.5 to 1.5
    // frames, where its coefficient stays small).
    int   whole = (int)(delay - 0.5f);
    float frac  = delay - (float)whole;
    float eta   = (1.0f - frac) / (1.0f + frac);
    int   i     = ln.pos - whole;
    if(i < 0)
        i += ln.size;
    int j = i == 0 ? ln.size - 1 : i - 1;
    ln.ap_y = eta * (ln.buf[i] - ln.ap_y) + ln.buf[j];
    return ln.ap_y;
}

void Sea::Process(float mid, float side, float *l, float *r)
{
    side *= in_side_;
    for(int d = 0; d < kDiffusers; d++)
    {
        mid  = diff_[0][d].Process(mid);
        side = diff_[1][d].Process(side);
    }
    mid *= kInGain;
    side *= kInGain;

    float m[kLines];
    float energy = 0.0f, out_l = 0.0f, out_r = 0.0f;
    for(int i = 0; i < kLines; i++)
    {
        Line &ln = lines_[i];
        ln.phase += ln.inc;
        if(ln.phase >= 1.0f)
            ln.phase -= 1.0f;
        ln.wind += ln.wind_step;
        float x = Read(ln, ln.delay + depth_ * (1.0f + FastSin(ln.phase)) + ln.wind);
        energy += x * x;
        out_l += kOutL[i] * x;
        out_r += kOutR[i] * x;

        ln.z    = ln.b * x + ln.a * ln.z;
        float h = kDcGain * (ln.z - ln.dc_x) + kDcPole * ln.dc_y;
        ln.dc_x = ln.z;
        ln.dc_y = h;
        if(surge_ > 0.0f)
            h = kClip * SoftClip(surge_gain_ * kInvClip * h);
        m[i] = h;
    }
    energy_ += energy;
    *l = kOutGain * out_l;
    *r = kOutGain * out_r;

    // The Hadamard matrix, as a fast Walsh-Hadamard transform.
    for(int len = 1; len < kLines; len <<= 1)
        for(int i = 0; i < kLines; i += len << 1)
            for(int j = i; j < i + len; j++)
            {
                float a  = m[j];
                float b  = m[j + len];
                m[j]       = a + b;
                m[j + len] = a - b;
            }

    float fb = kMatrix * wall_;
    for(int i = 0; i < kLines; i++)
    {
        Line &ln       = lines_[i];
        ln.buf[ln.pos] = fb * m[i] + kInMid[i] * mid + kInSide[i] * side;
        if(++ln.pos >= ln.size)
            ln.pos = 0;
    }
}

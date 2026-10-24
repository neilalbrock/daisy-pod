#include "post_fx.h"

#include <cmath>

using namespace dsp;

namespace
{
constexpr float kMaxSwingS = 0.006f; // pitch mod delay swing at full depth
constexpr float kMinCutoff = 20.0f;
constexpr float kMaxCutoff = 20000.0f;
} // namespace

void PostFx::Init(float sample_rate)
{
    sample_rate_ = sample_rate;
    depth_coef_  = SmoothCoef(0.05f, sample_rate);
    gain_coef_   = SmoothCoef(0.02f, sample_rate);
    block_coef_  = 0.1f; // per block: about 1 ms at block size 4
    svf_l_.Set(cutoff_, 0.707f, sample_rate);
    svf_r_.CopyCoefs(svf_l_);
}

void PostFx::SetParams(const Params &p)
{
    float depth   = Clamp(p.mod_depth, 0.0f, 1.0f);
    depth_target_ = depth * depth * kMaxSwingS * sample_rate_;
    float rate_hz = 0.05f * powf(200.0f, Clamp(p.mod_rate, 0.0f, 1.0f)); // to 10 Hz
    lfo_inc_      = rate_hz / sample_rate_;

    // Exponential sweep; the last few percent of travel kill the effect.
    float f      = Clamp(p.filter, 0.0f, 1.0f);
    float target = kMinCutoff * powf(kMaxCutoff / kMinCutoff, f);
    cutoff_ += block_coef_ * (target - cutoff_);
    float q = 0.5f * powf(24.0f, Clamp(p.resonance, 0.0f, 1.0f)); // 0.5 .. 12
    if(f > 0.9f)
        q += (0.707f - q) * (f - 0.9f) * 10.0f;
    svf_l_.Set(cutoff_, q, sample_rate_);
    svf_r_.CopyCoefs(svf_l_);

    float kill   = f < 0.03f ? f / 0.03f : 1.0f;
    gain_target_ = Clamp(p.volume, 0.0f, 1.0f) * 2.0f * kill;
}

float PostFx::ReadMod(const float *buf, float delay) const
{
    float  pos  = (float)mod_write_ - delay + (float)kModLen;
    size_t i0   = (size_t)pos;
    float  frac = pos - (float)i0;
    float  a    = buf[i0 & (kModLen - 1)];
    float  b    = buf[(i0 + 1) & (kModLen - 1)];
    return a + frac * (b - a);
}

void PostFx::Process(float *l, float *r)
{
    // Pitch modulation. The delay is depth * (0.5 + 0.5 sin), so at zero
    // depth there's no delay at all and nothing to switch out.
    mod_buf_l_[mod_write_] = *l;
    mod_buf_r_[mod_write_] = *r;
    depth_ += depth_coef_ * (depth_target_ - depth_);
    if(depth_ > 0.01f)
    {
        float d_l = depth_ * (0.5f + 0.5f * FastSin(lfo_phase_));
        float d_r = depth_ * (0.5f + 0.5f * FastSin(WrapPhase(lfo_phase_ + 0.25f)));
        *l        = ReadMod(mod_buf_l_, d_l);
        *r        = ReadMod(mod_buf_r_, d_r);
    }
    mod_write_ = (mod_write_ + 1) & (kModLen - 1);
    lfo_phase_ = WrapPhase(lfo_phase_ + lfo_inc_);

    float bp;
    *l = svf_l_.Process(*l, &bp);
    *r = svf_r_.Process(*r, &bp);

    gain_ += gain_coef_ * (gain_target_ - gain_);
    *l *= gain_;
    *r *= gain_;
}

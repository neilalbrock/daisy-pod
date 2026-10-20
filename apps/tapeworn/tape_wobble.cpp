#include "tape_wobble.h"

using namespace tape;

void Wow::Init(float sample_rate, uint32_t seed)
{
    sample_rate_ = sample_rate;
    rng_.Seed(seed);
    coef_  = 1.0f - expf(-kTwoPi * 1.2f / sample_rate);
    inc_   = rng_.Range(0.45f, 0.65f) / sample_rate;
    phase_ = rng_.Uni();
    hold_  = 0;
}

float Wow::Process()
{
    if(--hold_ <= 0)
    {
        target_ = rng_.Bi();
        hold_   = (int32_t)(rng_.Range(0.25f, 1.2f) * sample_rate_);
    }
    s1_ += coef_ * (target_ - s1_);
    s2_ += coef_ * (s1_ - s2_);

    phase_ = WrapPhase(phase_ + inc_);
    return 0.65f * s2_ + 0.35f * FastSin(phase_);
}

void Flutter::Init(float sample_rate, uint32_t seed)
{
    sample_rate_ = sample_rate;
    rng_.Seed(seed);
    inc1_       = rng_.Range(8.5f, 10.5f) / sample_rate;
    inc2_       = rng_.Range(14.0f, 17.0f) / sample_rate;
    phase1_     = rng_.Uni();
    phase2_     = rng_.Uni();
    drift_coef_ = SmoothCoef(0.15f, sample_rate);
    noise_coef_ = 1.0f - expf(-kTwoPi * 25.0f / sample_rate);
}

float Flutter::Process()
{
    // The capstan speed wanders by a few percent, so the flutter never
    // settles into a clean vibrato.
    if(--drift_hold_ <= 0)
    {
        drift_target_ = rng_.Range(0.92f, 1.08f);
        drift_hold_   = (int32_t)(rng_.Range(0.1f, 0.4f) * sample_rate_);
    }
    drift_ += drift_coef_ * (drift_target_ - drift_);

    phase1_ = WrapPhase(phase1_ + inc1_ * drift_);
    phase2_ = WrapPhase(phase2_ + inc2_ * drift_);
    // Four poles, not one: a single pole's -6 dB/octave tail jitters the
    // delay line at audio rates, which put a hiss-like skirt around every
    // note (about -44 dB at flutter 0.3, against -80 dB now).
    float n = rng_.Bi();
    for(auto &s : noise_)
    {
        s += noise_coef_ * (n - s);
        n = s;
    }

    // Low-passed white noise is small; x8.5 brings it up to about the sines
    // (and matches the single pole's x6 below 30 Hz).
    return 0.55f * FastSin(phase1_) + 0.25f * FastSin(phase2_)
           + 0.2f * Clamp(n * 8.5f, -1.0f, 1.0f);
}

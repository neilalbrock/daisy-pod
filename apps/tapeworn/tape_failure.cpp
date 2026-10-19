#include "tape_failure.h"

using namespace tape;

namespace
{
// Rate curve that stays at zero until `start`, then rises.
float Onset(float amount, float start)
{
    float x = Clamp((amount - start) / (1.0f - start), 0.0f, 1.0f);
    return x * sqrtf(x);
}
} // namespace

void TapeFailure::Init(float sample_rate, uint32_t seed)
{
    sample_rate_  = sample_rate;
    rng_.Seed(seed);
    crumble_coef_ = SmoothCoef(0.0003f, sample_rate);
    jitter_coef_  = SmoothCoef(0.0015f, sample_rate);
    drop_coef_    = SmoothCoef(0.0015f, sample_rate);
    rustle_filter_.Set(Biquad::Type::kHighPass, 1500.0f, 0.7f, 0.0f, sample_rate);
    pop_filter_.Set(Biquad::Type::kLowPass, 2500.0f, 2.0f, 0.0f, sample_rate);
    SetAmount(0.0f, 0.0f);
}

void TapeFailure::SetAmount(float amount, float extra)
{
    float f   = Lerp(Clamp(amount, 0.0f, 1.0f), 1.0f, extra);
    float mul = 1.0f + extra;

    amount_      = f;
    crinkle_hz_  = 2.5f * f * mul;
    pop_hz_      = 3.0f * f * sqrtf(f) * mul;
    snag_hz_     = 1.2f * Onset(f, 0.15f) * mul;
    drop_hz_     = 0.9f * Onset(f, 0.3f) * mul;
    sound_level_ = 0.05f * sqrtf(f);
}

void TapeFailure::StartCrinkle()
{
    crinkle_left_    = (int32_t)(rng_.Range(0.04f, 0.12f + 0.35f * amount_) * sample_rate_);
    crinkle_density_ = rng_.Range(300.0f, 600.0f + 1500.0f * amount_);
    crinkle_depth_   = rng_.Range(0.3f, 1.0f) * amount_;
}

TapeFailure::Out TapeFailure::Process()
{
    float rustle = 0.0f, pop = 0.0f;

    // Crinkles
    if(crinkle_left_ > 0)
    {
        if(--crinkle_left_ == 0)
        {
            crumble_target_ = 1.0f;
            jitter_target_  = 0.0f;
        }
        else if(rng_.Chance(crinkle_density_, sample_rate_))
        {
            float click     = rng_.Bi() * crinkle_depth_;
            rustle          = click;
            crumble_target_ = 1.0f - 0.5f * fabsf(click);
            jitter_target_  = click * 0.00005f * sample_rate_; // up to 50 us
        }
    }
    else if(rng_.Chance(crinkle_hz_, sample_rate_))
        StartCrinkle();

    // Pops
    if(pop_dip_left_ > 0 && --pop_dip_left_ == 0)
        pop_dip_target_ = 1.0f;
    if(rng_.Chance(pop_hz_, sample_rate_))
    {
        float level     = rng_.Range(0.3f, 1.0f);
        pop             = rng_.Uni() < 0.5f ? -level : level;
        pop_dip_target_ = 1.0f - 0.6f * level;
        pop_dip_left_   = (int32_t)(0.003f * sample_rate_);
    }

    // Snags
    if(snag_phase_ >= 1.0f && rng_.Chance(snag_hz_, sample_rate_))
    {
        snag_phase_ = 0.0f;
        snag_inc_   = 1.0f / (rng_.Range(0.02f, 0.08f) * sample_rate_);
        snag_amp_ = rng_.Range(0.3f, 1.0f) * (0.001f + 0.002f * amount_) * sample_rate_;
    }
    float snag = 0.0f;
    if(snag_phase_ < 1.0f)
    {
        // Raised cosine: the delay swells (pitch dips) then recovers (pitch
        // overshoots), like tape catching and lurching free.
        snag = snag_amp_ * 0.5f * (1.0f - FastSin(WrapPhase(snag_phase_ + 0.25f)));
        snag_phase_ += snag_inc_;
    }

    // Drops
    if(drop_left_ > 0)
    {
        if(--drop_left_ == 0)
            drop_target_ = 1.0f;
    }
    else if(rng_.Chance(drop_hz_, sample_rate_))
    {
        drop_left_ = (int32_t)(rng_.Range(0.015f, 0.05f + 0.25f * amount_) * sample_rate_);
        drop_target_ = 1.0f - rng_.Range(0.5f, 1.0f);
    }

    crumble_ += crumble_coef_ * (crumble_target_ - crumble_);
    pop_dip_ += crumble_coef_ * (pop_dip_target_ - pop_dip_);
    jitter_ += jitter_coef_ * (jitter_target_ - jitter_);
    drop_ += drop_coef_ * (drop_target_ - drop_);

    Out out;
    out.gain  = crumble_ * pop_dip_ * drop_;
    out.delay = snag + jitter_;
    out.sound = (rustle_filter_.Process(rustle) + pop_filter_.Process(pop))
                * sound_level_;
    return out;
}

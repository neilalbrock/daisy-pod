#include "fx_multidelay.h"

#include <cmath>

using namespace dsp;

namespace
{
// Tap times in steps.
const float kPatternTimes[4][MultiTap::kTaps] = {
    {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f},
    {0.75f, 1.5f, 2.25f, 3.0f, 3.75f, 4.5f, 5.25f, 6.0f},
    {0.667f, 1.333f, 2.0f, 2.667f, 3.333f, 4.0f, 4.667f, 5.333f},
    {0.25f, 0.5f, 0.75f, 2.0f, 2.25f, 2.5f, 4.0f, 4.25f},
};
const float kWarpTimes[MultiTap::kTaps] = {0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f};

// Warp C: +12, +7, -12, +5, +12, -5, -7, -12 semitones.
const float kWarpPitch[MultiTap::kTaps] = {2.0f, 1.4983f, 0.5f, 1.3348f, 2.0f, 0.7492f, 0.6674f, 0.5f};

constexpr float kFeedbackToneHz = 5000.0f;
constexpr float kPitchWindowS   = 0.06f;

// Level of the tap at x (0 = first, 1 = last active) for a Shape setting.
// None drops below 0.2, so every tap stays audible.
float TapLevel(Env shape, float x)
{
    switch(shape)
    {
        case Env::kSquare: return 1.0f;
        case Env::kRampUp: return 0.2f + 0.8f * x;
        case Env::kTriangle: return 0.2f + 0.8f * (1.0f - fabsf(2.0f * x - 1.0f));
        default: return 1.0f - 0.8f * x;
    }
}
} // namespace

// ---------------------------------------------------------------- TapReader

namespace
{
inline void ReadAt(const SampleBuffer &buf, float delay, float *l, float *r)
{
    // Frame positions are whole numbers, so split the delay rather than
    // subtracting it from the write position as a float.
    uint32_t whole = (uint32_t)delay;
    float    frac  = delay - (float)whole;
    buf.Read(buf.WritePos() - whole - 1, 1.0f - frac, l, r);
}
} // namespace

void TapReader::Read(const SampleBuffer &buf, float *l, float *r)
{
    Glide();
    if(!pitched_)
    {
        ReadAt(buf, delay_, l, r);
        return;
    }

    // Each head's delay moves at 1 - rate frames per frame, so it reads at
    // `rate`, and jumps back by a window when it runs out.
    phase_   = WrapPhase(phase_ + inc_);
    float f0 = phase_, f1 = WrapPhase(phase_ + 0.5f);
    float d0 = delay_ + window_ * (rate_ < 1.0f ? f0 : 1.0f - f0);
    float d1 = delay_ + window_ * (rate_ < 1.0f ? f1 : 1.0f - f1);
    float g0 = 1.0f - fabsf(2.0f * f0 - 1.0f); // triangles a half-window
    float g1 = 1.0f - fabsf(2.0f * f1 - 1.0f); // apart sum to 1

    float l0, r0, l1, r1;
    ReadAt(buf, d0, &l0, &r0);
    ReadAt(buf, d1, &l1, &r1);
    *l = g0 * l0 + g1 * l1;
    *r = g0 * r0 + g1 * r1;
}

// ---------------------------------------------------------------- MultiTap

void MultiTap::Init(float sample_rate)
{
    sample_rate_ = sample_rate;
    tone_        = 1.0f - expf(-kTwoPi * kFeedbackToneHz / sample_rate);
    for(int k = 0; k < kTaps; k++)
    {
        taps_[k].Init(sample_rate);
        alt_[k].Init(sample_rate);
    }
}

void MultiTap::Reset()
{
    for(int k = 0; k < kTaps; k++)
    {
        taps_[k].Reset();
        alt_[k].Reset();
        svf_l_[k].Reset();
        svf_r_[k].Reset();
    }
    lp_l_ = lp_r_ = 0.0f;
    use_alt_      = false;
    filter_       = TapFilter::kNone;
}

void MultiTap::Setup(const EffectContext &ctx, const float (&times)[kTaps])
{
    float sr  = ctx.sample_rate;
    count_    = 1 + (int)(ctx.activity * 7.99f);
    window_   = Clamp(ctx.step_frames, 0.05f * sr, 0.5f * sr);
    feedback_ = 0.92f * ctx.repeats;

    // Taps are mostly different moments, so their levels add as powers.
    float levels[kTaps], sum = 0.0f;
    for(int k = 0; k < count_; k++)
    {
        levels[k] = TapLevel(ctx.shape, count_ > 1 ? (float)k / (count_ - 1) : 0.0f);
        sum += levels[k] * levels[k];
    }
    float norm = 0.8f / sqrtf(sum > 1.0f ? sum : 1.0f);

    // Stay clear of the part of the buffer about to be recorded over.
    float max_delay = 0.7f * (float)ctx.buffer_frames - window_;
    for(int k = 0; k < count_; k++)
    {
        float pan  = (k & 1) ? 0.75f : 0.25f;
        gain_l_[k] = norm * levels[k] * 1.41421356f * FastSin(0.25f * (1.0f - pan));
        gain_r_[k] = norm * levels[k] * 1.41421356f * FastSin(0.25f * pan);
        rate_[k]   = ctx.reverse ? -1.0f : 1.0f;
        delay_[k]  = Clamp(times[k] * ctx.step_frames, 2.0f, max_delay);
        taps_[k].Set(delay_[k], rate_[k], window_);
    }
}

void MultiTap::Process(const SampleBuffer &buf,
                       float,
                       float,
                       float *l,
                       float *r,
                       float *fb_l,
                       float *fb_r)
{
    float sum_l = 0.0f, sum_r = 0.0f, last_l = 0.0f, last_r = 0.0f;
    for(int k = 0; k < count_; k++)
    {
        float tl, tr;
        if(!use_alt_)
            taps_[k].Read(buf, &tl, &tr);
        else
        {
            // Warp D: only read both while crossfading.
            float mix = alt_mix_[k];
            if(mix < 1.0f)
                taps_[k].Read(buf, &tl, &tr);
            else
                taps_[k].Skip();
            if(mix > 0.0f)
            {
                float al, ar;
                alt_[k].Read(buf, &al, &ar);
                tl = mix < 1.0f ? tl + (al - tl) * mix : al;
                tr = mix < 1.0f ? tr + (ar - tr) * mix : ar;
            }
            else
                alt_[k].Skip();
        }
        // Feedback takes the tap before its filter, so Repeats sustains as
        // well in every variation and the filters only colour what's heard.
        last_l = tl;
        last_r = tr;
        if(filter_ != TapFilter::kNone)
        {
            float bp_l, bp_r;
            float lp_l = svf_l_[k].Process(tl, &bp_l);
            float lp_r = svf_r_[k].Process(tr, &bp_r);
            tl         = filter_ == TapFilter::kLowPass ? lp_l : bp_l;
            tr         = filter_ == TapFilter::kLowPass ? lp_r : bp_r;
        }
        sum_l += tl * gain_l_[k];
        sum_r += tr * gain_r_[k];
    }
    *l += sum_l;
    *r += sum_r;

    // The last tap feeds back: the pattern repeats from the top.
    lp_l_ += tone_ * (last_l - lp_l_);
    lp_r_ += tone_ * (last_r - lp_r_);
    *fb_l += SoftClip(dc_l_.Process(lp_l_) * feedback_);
    *fb_r += SoftClip(dc_r_.Process(lp_r_) * feedback_);
}

int MultiTap::Heads(const SampleBuffer &buf, weblink::Head *out, int max) const
{
    int n = count_ < max ? count_ : max;
    for(int k = 0; k < n; k++)
    {
        float          rate = taps_[k].Rate();
        weblink::Head &h    = out[k];
        h.pos               = buf.WritePos() - (uint32_t)taps_[k].Delay();
        h.start             = h.pos;
        h.length            = 0.0f;
        h.rate              = fabsf(rate);
        h.reverse           = rate < 0.0f;
        h.tap               = true;
        h.amp               = Clamp(0.7f * (gain_l_[k] + gain_r_[k]), 0.0f, 1.0f);
        h.pan               = (k & 1) ? 0.75f : 0.25f; // as Setup() pans them
    }
    return n;
}

// ---------------------------------------------------------------- Pattern

void FxPattern::Block(const EffectContext &ctx)
{
    Setup(ctx, kPatternTimes[ctx.variation]);
}

// ---------------------------------------------------------------- Warp

void FxWarp::Reset()
{
    MultiTap::Reset();
    env_   = 0.0f;
    phase_ = 0.0f;
}

void FxWarp::Block(const EffectContext &ctx)
{
    Setup(ctx, kWarpTimes);
    float sr  = ctx.sample_rate;
    float dir = ctx.reverse ? -1.0f : 1.0f;

    switch(ctx.variation)
    {
        case 0:
        {
            // Opens fully at about -18 dBFS on the first tap; later taps
            // need more.
            filter_ = TapFilter::kLowPass;
            env_ += 0.05f * (ctx.input_level - env_);
            for(int k = 0; k < count_; k++)
            {
                // Its bright feedback builds up more than the others'.
                gain_l_[k] *= 0.75f;
                gain_r_[k] *= 0.75f;
                float sens   = 8.0f * (1.0f - 0.08f * k);
                float cutoff = 250.0f * powf(60.0f, Clamp(env_ * sens, 0.0f, 1.0f));
                svf_l_[k].Set(cutoff, 0.9f, sr);
                svf_r_[k].CopyCoefs(svf_l_[k]);
            }
            break;
        }
        case 1:
            filter_ = TapFilter::kBandPass;
            for(int k = 0; k < count_; k++)
            {
                svf_l_[k].Set(300.0f * powf(2.0f, 0.5f * k), 6.0f, sr); // 300 Hz to 3.4 kHz
                svf_r_[k].CopyCoefs(svf_l_[k]);
                // Make up what the narrow band takes away.
                gain_l_[k] *= 2.5f;
                gain_r_[k] *= 2.5f;
            }
            break;
        case 2:
            filter_ = TapFilter::kNone;
            for(int k = 0; k < count_; k++)
                taps_[k].Set(delay_[k], dir * kWarpPitch[k], kPitchWindowS * sr);
            break;
        default:
            filter_  = TapFilter::kNone;
            use_alt_ = true;
            phase_   = WrapPhase(phase_ + ctx.block_frames / (2.0f * ctx.beat_frames));
            for(int k = 0; k < count_; k++)
            {
                alt_[k].Set(delay_[k], dir * 2.0f, kPitchWindowS * sr);
                // Rests on each sound and fades for about a fifth of the
                // cycle, so both are read only then.
                float s     = FastSin(WrapPhase(phase_ + (float)k / kTaps));
                alt_mix_[k] = Clamp(0.5f + 1.5f * s, 0.0f, 1.0f);
            }
            break;
    }
}

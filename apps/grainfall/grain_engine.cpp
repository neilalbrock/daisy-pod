#include "grain_engine.h"

#include <cmath>

using namespace dsp;

namespace
{
// Time knob subdivisions as multiples of the beat rate.
constexpr float kSubdivMult[GrainEngine::kNumSubdivs] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f};

constexpr float kMinHoldS = 0.25f;
constexpr float kMaxHoldS = 2.0f;

// Each trip round the feedback loop loses a little top end, like a tape
// echo, so long tails darken instead of turning harsh.
constexpr float kFeedbackToneHz = 6000.0f;

// tanh-like soft clip, exact enough up to |x| = 3 and clamped beyond.
float SoftClip(float x)
{
    x = Clamp(x, -3.0f, 3.0f);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}
} // namespace

void GrainEngine::Init(float  sample_rate,
                       float *buffer,
                       size_t buffer_frames,
                       float *hold_buffer,
                       size_t hold_frames)
{
    sample_rate_ = sample_rate;
    buffer_.Init(buffer, buffer_frames);
    voices_.Init(sample_rate);
    hold_.Init(sample_rate, hold_buffer, hold_frames);
    onset_detector_.Init(sample_rate);
    onsets_.Clear();
    post_.Init(sample_rate);
    preroll_ = (uint32_t)(0.005f * sample_rate);
    first_   = true;
    fb_coef_ = SmoothCoef(0.05f, sample_rate);
    fb_tone_ = 1.0f - expf(-kTwoPi * kFeedbackToneHz / sample_rate);
}

Effect *GrainEngine::EffectFor(EffectId id)
{
    switch(id)
    {
        case EffectId::kHaze: return &haze_;
        case EffectId::kTunnel: return &tunnel_;
        case EffectId::kStrum: return &strum_;
        // The placeholder cloud until these are written.
        default: return &cloud_;
    }
}

void GrainEngine::BeginBlock(const Params &p, int block_frames)
{
    bool changed = first_ || p.effect != params_.effect
                   || p.variation != params_.variation;
    params_ = p;
    first_  = false;

    int   subdiv = p.subdiv < 0 ? 0 : (p.subdiv >= kNumSubdivs ? kNumSubdivs - 1 : p.subdiv);
    float mult   = kSubdivMult[subdiv];

    EffectContext ctx;
    ctx.variation     = p.variation & 3;
    ctx.activity      = Clamp(p.activity, 0.0f, 1.0f);
    ctx.repeats       = Clamp(p.repeats, 0.0f, 1.0f);
    ctx.shape         = (Env)(p.shape & 3);
    ctx.reverse       = p.reverse;
    ctx.sample_rate   = sample_rate_;
    ctx.block_frames  = block_frames;
    ctx.beat_frames   = p.samples_per_beat;
    ctx.step_frames   = p.samples_per_beat / mult;
    ctx.step_pos      = p.beat_pos * mult;
    ctx.step_index    = (int32_t)floorf(ctx.step_pos);
    ctx.step          = ctx.step_index != last_step_;
    ctx.onset         = onset_;
    ctx.onsets        = &onsets_;
    ctx.write_pos     = buffer_.WritePos();
    ctx.buffer_frames = buffer_.Frames();
    ctx.voices        = &voices_;
    ctx.rng           = &rng_;
    last_step_        = ctx.step_index;
    onset_            = false;

    hold_len_ = Clamp(ctx.step_frames, kMinHoldS * sample_rate_, kMaxHoldS * sample_rate_);

    if(changed)
    {
        voices_.ReleaseAll();
        effect_ = EffectFor(p.effect);
        effect_->Reset();
    }
    effect_->Block(ctx);
    fb_target_ = Clamp(effect_->Feedback(ctx.repeats), 0.0f, 1.5f);

    post_.SetParams(p.post);
}

void GrainEngine::Process(float in_l, float in_r, float *out_l, float *out_r)
{
    float src_l, src_r;
    hold_.Process(params_.hold, hold_len_, in_l, in_r, &src_l, &src_r);

    if(onset_detector_.Process(0.5f * (src_l + src_r)))
    {
        onsets_.Add(buffer_.WritePos() - preroll_);
        onset_ = true;
    }

    // Voices only read frames that are already recorded, so this frame can
    // be written after them, with their output fed back in.
    float l, r;
    voices_.Process(buffer_, &l, &r);
    effect_->Process(src_l, src_r, &l, &r);

    fb_ += fb_coef_ * (fb_target_ - fb_);
    fb_lp_l_ += fb_tone_ * (l - fb_lp_l_);
    fb_lp_r_ += fb_tone_ * (r - fb_lp_r_);
    float fb_l = SoftClip(fb_dc_l_.Process(fb_lp_l_) * fb_);
    float fb_r = SoftClip(fb_dc_r_.Process(fb_lp_r_) * fb_);
    buffer_.Write(src_l + fb_l, src_r + fb_r);

    post_.Process(&l, &r);

    *out_l = l;
    *out_r = r;
}

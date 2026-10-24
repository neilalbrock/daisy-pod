#include "grain_engine.h"

#include <cmath>

using namespace dsp;

namespace
{
// Time knob subdivisions as multiples of the beat rate.
constexpr float kSubdivMult[GrainEngine::kNumSubdivs] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f};

constexpr float kMinHoldS = 0.25f;
constexpr float kMaxHoldS = 2.0f;
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
}

Effect *GrainEngine::EffectFor(EffectId)
{
    // Every slot plays the placeholder cloud until its effect is written.
    return &cloud_;
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
    ctx.step_index    = (int32_t)floorf(p.beat_pos * mult);
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
    buffer_.Write(src_l, src_r);

    float l, r;
    voices_.Process(buffer_, &l, &r);
    effect_->Process(src_l, src_r, &l, &r);
    post_.Process(&l, &r);

    *out_l = l;
    *out_r = r;
}

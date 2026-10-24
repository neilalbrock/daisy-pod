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
} // namespace

void GrainEngine::Init(float  sample_rate,
                       float *buffer,
                       size_t buffer_frames,
                       float *hold_buffer,
                       size_t hold_frames,
                       float *mod_buffer)
{
    sample_rate_ = sample_rate;
    buffer_.Init(buffer, buffer_frames);
    voices_.Init(sample_rate);
    hold_.Init(sample_rate, hold_buffer, hold_frames);
    onset_detector_.Init(sample_rate);
    onsets_.Clear();
    post_.Init(sample_rate, mod_buffer);
    pattern_.Init(sample_rate);
    warp_.Init(sample_rate);
    preroll_ = (uint32_t)(0.005f * sample_rate);
    first_   = true;
    fb_coef_ = SmoothCoef(0.05f, sample_rate);
    fb_tone_ = 1.0f - expf(-kTwoPi * kFeedbackToneHz / sample_rate);
}

Effect *GrainEngine::EffectFor(EffectId id)
{
    switch(id)
    {
        case EffectId::kMosaic: return &mosaic_;
        case EffectId::kSeq: return &seq_;
        case EffectId::kGlide: return &glide_;
        case EffectId::kHaze: return &haze_;
        case EffectId::kTunnel: return &tunnel_;
        case EffectId::kStrum: return &strum_;
        case EffectId::kBlocks: return &blocks_;
        case EffectId::kInterrupt: return &interrupt_;
        case EffectId::kArp: return &arp_;
        case EffectId::kPattern: return &pattern_;
        case EffectId::kWarp: return &warp_;
    }
    return &haze_;
}

float GrainEngine::StepsPerBeat(int subdiv)
{
    return kSubdivMult[subdiv < 0 ? 0 : (subdiv >= kNumSubdivs ? kNumSubdivs - 1 : subdiv)];
}

int GrainEngine::Heads(weblink::Head *out, int max) const
{
    int n = voices_.Heads(out, max);
    return effect_ != nullptr ? n + effect_->Heads(buffer_, out + n, max - n) : n;
}

void GrainEngine::SetOverview(uint8_t *columns, size_t count)
{
    uint32_t frames = buffer_.Frames() / (uint32_t)count;
    overview_shift_ = 0;
    while((1u << overview_shift_) < frames)
        overview_shift_++;
    overview_mask_ = frames - 1;
    overview_peak_ = 0.0f;
    for(size_t i = 0; i < count; i++)
        columns[i] = 0;
    overview_ = columns;
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
    ctx.input_level   = onset_detector_.Envelope();
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
    float l, r, fx_fb_l = 0.0f, fx_fb_r = 0.0f;
    voices_.Process(buffer_, &l, &r);
    effect_->Process(buffer_, src_l, src_r, &l, &r, &fx_fb_l, &fx_fb_r);

    fb_ += fb_coef_ * (fb_target_ - fb_);
    fb_lp_l_ += fb_tone_ * (l - fb_lp_l_);
    fb_lp_r_ += fb_tone_ * (r - fb_lp_r_);
    float fb_l = SoftClip(fb_dc_l_.Process(fb_lp_l_) * fb_);
    float fb_r = SoftClip(fb_dc_r_.Process(fb_lp_r_) * fb_);
    float rec_l = src_l + fb_l + fx_fb_l, rec_r = src_r + fb_r + fx_fb_r;
    buffer_.Write(rec_l, rec_r);
    if(overview_ != nullptr)
    {
        overview_peak_ = fmaxf(overview_peak_, fmaxf(fabsf(rec_l), fabsf(rec_r)));
        if((buffer_.WritePos() & overview_mask_) == 0)
        {
            uint32_t col   = ((buffer_.WritePos() - 1) & (buffer_.Frames() - 1)) >> overview_shift_;
            overview_[col] = (uint8_t)(127.0f * sqrtf(fminf(overview_peak_, 1.0f)) + 0.5f);
            overview_peak_ = 0.0f;
        }
    }

    post_.Process(&l, &r);

    *out_l = l;
    *out_r = r;
}

#include "fx_micro_loop.h"

#include <cmath>

using namespace dsp;

namespace
{
// Steps between recaptures across Repeats.
const int kHoldSteps[] = {1, 2, 3, 4, 6, 8, 12, 16, 32};
constexpr int kNumHolds = sizeof(kHoldSteps) / sizeof(kHoldSteps[0]);

// A slice of `length` buffer frames from `start`, looped or not, with the
// common settings filled in.
VoiceParams Slice(const EffectContext &ctx, uint32_t start, float length, float rate)
{
    VoiceParams p;
    p.start   = start;
    p.length  = length;
    p.rate    = rate;
    p.reverse = ctx.reverse;
    p.env     = ctx.shape;
    p.soft    = 0.02f;
    return p;
}

// The Shape setting as a path from 0 to 1 and back over phase 0..1.
float GlidePath(Env shape, float phase)
{
    switch(shape)
    {
        case Env::kSquare: return phase < 0.5f ? 0.0f : 1.0f;
        case Env::kRampUp: return phase;
        case Env::kTriangle: return 1.0f - fabsf(2.0f * phase - 1.0f);
        default: return 1.0f - phase;
    }
}
} // namespace

int MicroLoopBase::HoldSteps(float repeats)
{
    int i = (int)(repeats * (float)kNumHolds);
    return kHoldSteps[i < kNumHolds ? i : kNumHolds - 1];
}

float MicroLoopBase::LoopFrames(const EffectContext &ctx)
{
    return Clamp(ctx.step_frames, 0.05f * ctx.sample_rate, 2.0f * ctx.sample_rate);
}

bool MicroLoopBase::Stale(const EffectContext &ctx, uint32_t start)
{
    return (float)(int32_t)(ctx.write_pos - start) > 0.7f * (float)ctx.buffer_frames;
}

// ---------------------------------------------------------------- Mosaic

namespace
{
const float kMosaicSpeeds[4][4] = {
    {1.0f, 2.0f, 1.0f, 2.0f},
    {1.0f, 0.5f, 1.0f, 0.5f},
    {2.0f, 2.0f, 2.0f, 2.0f},
    {0.5f, 1.0f, 2.0f, 4.0f},
};
} // namespace

void FxMosaic::Reset()
{
    for(auto &lp : loopers_)
        lp = Looper();
    steps_ = 0;
}

void FxMosaic::Block(const EffectContext &ctx)
{
    int n = 1 + (int)(ctx.activity * 3.99f);
    for(int i = n; i < kMaxLoopers; i++)
        ctx.voices->Release(loopers_[i].voice);

    if(!ctx.step)
        return;
    steps_++;

    int   hold = HoldSteps(ctx.repeats);
    float loop = LoopFrames(ctx);
    for(int i = 0; i < n; i++)
    {
        Looper &lp = loopers_[i];
        // Loopers take turns, so with a long hold they refresh one a step.
        bool due = hold > 0 && (steps_ + (uint32_t)(kMaxLoopers - i)) % (uint32_t)hold == 0;
        if(!due && ctx.voices->Active(lp.voice) && !Stale(ctx, lp.start))
            continue;

        float       rate   = kMosaicSpeeds[ctx.variation][i];
        float       length = loop * rate;
        VoiceParams p      = Slice(ctx, ctx.SliceStart(length, 0.0f), length, rate);
        p.passes           = 0;
        p.gain             = 0.9f / sqrtf((float)n);
        p.pan              = n == 1 ? 0.5f : 0.2f + 0.6f * (float)i / (float)(n - 1);

        ctx.voices->Release(lp.voice);
        lp.voice = ctx.voices->Start(p);
        lp.start = p.start;
    }
}

// ---------------------------------------------------------------- Seq

void FxSeq::Reset()
{
    pos_          = 0;
    held_         = 0;
    have_pattern_ = false;
    last_half_    = -1;
    next_sweep_   = 0;
    for(auto &h : sweep_)
        h = 0;
    steps_ = 0;
}

void FxSeq::Generate(const EffectContext &ctx, float loop)
{
    dsp::Rng &rng  = *ctx.rng;
    float     a    = ctx.activity;
    float     span = ctx.variation == 2 ? 2.0f : 1.0f; // C's slices are two steps
    float     rest = ctx.variation == 0 ? 0.2f : (ctx.variation == 3 ? 0.25f : 0.12f);

    for(int s = 0; s < kSteps; s++)
    {
        Slot &slot = pattern_[s];
        for(int l = 0; l < kLayers; l++)
        {
            float back    = (float)(int)(rng.Uni() * kSteps); // whole steps back
            slot.start[l] = ctx.SliceStart(loop * span, back * loop);
        }
        slot.rest   = rng.Uni() < rest;
        slot.rate   = ctx.variation == 1 && rng.Uni() < 0.8f * a ? 0.5f : 1.0f;
        slot.cutoff = a > 0.05f ? 8000.0f * powf(0.04f, a * rng.Uni()) : 0.0f;

        Slot &off    = offbeat_[s];
        off.start[0] = ctx.SliceStart(loop * 0.5f, (float)(int)(rng.Uni() * kSteps) * loop);
        off.rest     = rng.Uni() < 0.5f;
        off.rate     = 1.0f;
        off.cutoff   = 0.0f;
    }
    have_pattern_ = true;
    held_         = 0;
}

void FxSeq::PlayStep(const EffectContext &ctx, float loop)
{
    const Slot &s = pattern_[pos_];
    float       a = ctx.activity;

    switch(ctx.variation)
    {
        case 0:
            if(!s.rest)
            {
                VoiceParams p = Slice(ctx, s.start[0], loop, 1.0f);
                p.gain        = 0.9f;
                p.pan         = 0.5f + (ctx.rng->Uni() - 0.5f) * 0.5f;
                if(s.cutoff > 0.0f)
                {
                    p.filter = VoiceFilter::kLowPass;
                    p.cutoff = s.cutoff;
                    p.q      = 1.5f;
                }
                ctx.voices->Start(p);
            }
            break;

        case 1:
            if(!s.rest)
            {
                // The slice is shortened at half speed so it still fills
                // exactly one step.
                VoiceParams p = Slice(ctx, s.start[0], loop * s.rate, s.rate);
                p.gain        = 0.9f;
                ctx.voices->Start(p);
            }
            if(a > 0.95f && pos_ % 4 == 0)
            {
                // Sustainer: the last two steps at half speed, a soft pad
                // over four.
                VoiceParams p = Slice(ctx, ctx.SliceStart(loop * 2.0f, 0.0f), loop * 2.0f, 0.5f);
                p.env         = Env::kHann;
                p.gain        = 0.35f;
                p.filter      = VoiceFilter::kLowPass;
                p.cutoff      = 1500.0f;
                ctx.voices->Start(p);
            }
            break;

        case 2:
        {
            int layers = 1 + (int)(a * 2.99f);
            if(s.rest)
                break;
            for(int l = 0; l < layers; l++)
            {
                VoiceParams p = Slice(ctx, s.start[l], loop * 2.0f, 1.0f);
                p.soft        = 0.3f;
                p.gain        = 0.8f / sqrtf((float)layers);
                p.pan         = layers == 1 ? 0.5f : 0.2f + 0.6f * (float)l / (float)(layers - 1);
                p.filter      = VoiceFilter::kLowPass;
                p.cutoff      = 300.0f;
                sweep_[next_sweep_] = ctx.voices->Start(p);
                next_sweep_         = (next_sweep_ + 1) % 6;
            }
            break;
        }

        default:
            if(!s.rest)
            {
                VoiceParams p = Slice(ctx, s.start[0], loop, 1.0f);
                p.gain        = 0.8f;
                ctx.voices->Start(p);
            }
            if(a > 0.9f && pos_ % 2 == 0)
            {
                // Bit-crushed, an octave down over two steps.
                VoiceParams p = Slice(ctx, s.start[1], loop, 0.5f);
                p.gain        = 0.5f;
                p.crush_bits  = 6;
                p.crush_hold  = 4;
                ctx.voices->Start(p);
            }
            break;
    }
}

void FxSeq::Block(const EffectContext &ctx)
{
    float loop = LoopFrames(ctx);

    if(ctx.step)
    {
        steps_++;
        int hold = HoldSteps(ctx.repeats);
        if(!have_pattern_ || (hold > 0 && held_ >= hold)
           || Stale(ctx, pattern_[0].start[0]))
            Generate(ctx, loop);
        PlayStep(ctx, loop);
        pos_ = (pos_ + 1) % kSteps;
        held_++;
    }

    // D: a second layer on the half-steps between.
    if(ctx.variation == 3 && ctx.activity > 0.3f && have_pattern_)
    {
        int32_t half = (int32_t)floorf(ctx.step_pos * 2.0f);
        if(half != last_half_ && (half & 1))
        {
            const Slot &off = offbeat_[pos_];
            if(!off.rest)
            {
                VoiceParams p = Slice(ctx, off.start[0], loop * 0.5f, 1.0f);
                p.gain        = 0.6f;
                p.pan         = ctx.rng->Uni() < 0.5f ? 0.25f : 0.75f;
                ctx.voices->Start(p);
            }
        }
        last_half_ = half;
    }

    // C: the low-pass opens across each pass of the sequence.
    if(ctx.variation == 2)
    {
        float frac   = ctx.step_pos - floorf(ctx.step_pos);
        float phase  = ((float)pos_ + frac) / (float)kSteps;
        float cutoff = 300.0f * powf(25.0f, phase); // 300 Hz to 7.5 kHz
        for(auto h : sweep_)
            ctx.voices->SetFilter(h, cutoff, 2.0f);
    }
}

// ---------------------------------------------------------------- Glide

namespace
{
const float kGlideRange[4][2] = {
    {0.5f, 1.0f},
    {2.0f, 0.5f},
    {1.0f, 2.0f},
    {0.5f, 2.0f},
};
} // namespace

void FxGlide::Reset()
{
    for(auto &lp : loopers_)
        lp = Looper();
    phase_ = 0.0f;
    steps_ = 0;
}

void FxGlide::Block(const EffectContext &ctx)
{
    float sr   = ctx.sample_rate;
    float loop = Clamp(ctx.step_frames, 0.05f * sr, 1.0f * sr);

    float cycles_per_beat = 0.0625f * powf(32.0f, ctx.activity);
    phase_ = WrapPhase(phase_ + cycles_per_beat * ctx.block_frames / ctx.beat_frames);
    float v = GlidePath(ctx.shape, phase_);

    // Glide in octaves, so it sounds even.
    float lo = kGlideRange[ctx.variation][0], hi = kGlideRange[ctx.variation][1];
    float rates[2];
    rates[0] = lo * powf(hi / lo, v);
    rates[1] = ctx.variation == 3 ? lo * powf(hi / lo, 1.0f - v) : rates[0];

    if(ctx.step)
    {
        steps_++;
        int hold = HoldSteps(ctx.repeats);
        for(int i = 0; i < 2; i++)
        {
            Looper &lp = loopers_[i];
            // The two take turns, so one is always mid-loop when the other
            // starts again.
            bool due = hold > 0 && steps_ % (uint32_t)(2 * hold) == (uint32_t)(i * hold);
            if(!due && ctx.voices->Active(lp.voice) && !Stale(ctx, lp.start))
                continue;

            VoiceParams p = Slice(ctx, ctx.SliceStart(loop, 0.0f), loop, rates[i]);
            p.passes      = 0;
            p.env         = Env::kSquare; // Shape steers the glide instead
            p.soft        = 0.15f;
            p.gain        = 0.7f;
            p.pan         = i == 0 ? 0.3f : 0.7f;

            ctx.voices->Release(lp.voice);
            lp.voice = ctx.voices->Start(p);
            lp.start = p.start;
        }
    }

    for(int i = 0; i < 2; i++)
        ctx.voices->SetRate(loopers_[i].voice, rates[i]);
}

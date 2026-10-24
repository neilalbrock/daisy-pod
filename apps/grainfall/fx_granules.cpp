#include "fx_granules.h"

#include <cmath>

using namespace dsp;

// ---------------------------------------------------------------- Haze

namespace
{
struct HazeVariation
{
    float min_s, max_s;           // grain length range
    float rates[2];               // playback speeds, picked at random
    int   num_rates;
    float min_density, max_density; // grains per second across Activity
    bool  stretch;
};

const HazeVariation kHaze[4] = {
    {0.15f, 0.40f, {1.0f, 1.0f}, 1, 3.0f, 30.0f, true},
    {0.02f, 0.08f, {1.0f, 1.0f}, 1, 10.0f, 150.0f, false},
    {0.06f, 0.20f, {1.0f, 2.0f}, 2, 4.0f, 50.0f, false},
    {0.06f, 0.20f, {1.0f, 0.5f}, 2, 4.0f, 50.0f, false},
};

// Average grains overlapping at once, to stay inside the voice pool.
constexpr float kMaxOverlap = 12.0f;
constexpr float kHazeSoft   = 0.25f;
constexpr float kHazeTrim   = 0.7f;
} // namespace

float FxHaze::Feedback(float repeats) const
{
    // Most of the knob is the first two thirds of the way to the longest
    // wash, where small changes matter most.
    // Divided by the trim, so the gain round the loop is what's asked for.
    return 0.95f * sqrtf(repeats) / kHazeTrim;
}

void FxHaze::Reset()
{
    acc_ = 0.0f;
    lag_ = 0.0f;
}

void FxHaze::Block(const EffectContext &ctx)
{
    const HazeVariation &v  = kHaze[ctx.variation];
    float                sr = ctx.sample_rate;

    float mean_s  = 0.5f * (v.min_s + v.max_s);
    float density = v.min_density * powf(v.max_density / v.min_density, ctx.activity);
    density       = fminf(density, kMaxOverlap / mean_s);
    // Grains are uncorrelated, so their levels add as powers. Allowing for
    // the envelope keeps every shape at the same level, and keeps the
    // feedback loop's gain close to the feedback setting.
    float power  = density * mean_s * EnvPower(ctx.shape, kHazeSoft);
    float gain   = power > 1.0f ? 1.0f / sqrtf(power) : 1.0f;
    // Grains read close together still add partly in phase; this brings
    // the cloud back to about the input's level.
    gain *= kHazeTrim;
    float reach  = Clamp(ctx.step_frames, 0.05f * sr, 2.0f * sr);
    float spread = 0.3f + 0.7f * ctx.activity;

    // A: a play head moving at half speed falls behind the input until it
    // is a whole reach back, then jumps forward again. The overlapping
    // grains hide the jump.
    if(v.stretch)
    {
        lag_ += 0.5f * ctx.block_frames;
        if(lag_ > reach)
            lag_ = 0.0f;
    }

    // Jittered so the grains don't fall into a rhythm.
    acc_ += density * ctx.block_frames / sr * (0.5f + ctx.rng->Uni());
    while(acc_ >= 1.0f)
    {
        acc_ -= 1.0f;

        float rate   = v.rates[ctx.rng->Uni() < 0.5f ? 0 : v.num_rates - 1];
        float size   = (v.min_s + (v.max_s - v.min_s) * ctx.rng->Uni()) * sr;
        float length = size * rate;
        float delay  = v.stretch ? lag_ + ctx.rng->Uni() * spread * 0.1f * sr
                                 : ctx.rng->Uni() * spread * reach;

        VoiceParams p;
        p.start   = ctx.SliceStart(length, delay);
        p.length  = length;
        p.rate    = rate;
        p.reverse = ctx.reverse;
        p.env     = ctx.shape;
        p.soft    = kHazeSoft;
        p.gain    = gain;
        p.pan     = 0.5f + (ctx.rng->Uni() - 0.5f) * spread;
        ctx.voices->Start(p);
    }
}

// ---------------------------------------------------------------- Tunnel

namespace
{
constexpr float kTunnelSkipS = 0.01f; // start the loop just after the attack
} // namespace

void FxTunnel::Reset()
{
    for(auto &d : drones_)
        d = Drone();
    next_    = 0;
    pending_ = false;
    squeeze_ = 0.0f;
}

void FxTunnel::StartDrone(const EffectContext &ctx, float length)
{
    Drone &d = drones_[next_];
    ctx.voices->Release(d.voice); // the oldest makes room

    VoiceParams p;
    p.start   = pending_start_;
    p.length  = length;
    p.rate    = ctx.variation == 1 ? 0.5f : 1.0f;
    p.reverse = ctx.reverse;
    p.passes  = 0;
    p.env     = ctx.shape;
    p.gain    = 0.6f;
    p.pan     = 0.5f + ((next_ & 1) ? 0.3f : -0.3f);
    if(ctx.variation == 1)
    {
        p.filter = VoiceFilter::kLowPass;
        p.cutoff = 800.0f;
        p.q      = 1.5f;
    }
    else if(ctx.variation == 2)
    {
        d.centre = 300.0f * powf(10.0f, ctx.rng->Uni()); // 300 Hz to 3 kHz
        p.filter = VoiceFilter::kBandPass;
        p.cutoff = d.centre;
        p.q      = 1.0f + 15.0f * ctx.activity;
        // A band-pass keeps a small part of a broad sound; make up some of
        // the loss, more as it narrows.
        p.gain *= 1.0f + 0.5f * sqrtf(p.q);
    }
    d.voice = ctx.voices->Start(p);
    d.phase = ctx.rng->Uni();
    next_   = (next_ + 1) % kMaxDrones;
}

void FxTunnel::Block(const EffectContext &ctx)
{
    float sr     = ctx.sample_rate;
    float a      = ctx.activity;
    float length = Clamp(ctx.step_frames * 0.125f, 0.02f * sr, 0.4f * sr);
    float rate   = ctx.variation == 1 ? 0.5f : 1.0f;

    // Each pass round the loop is quieter by enough to reach -60 dB after
    // the decay time. At the top of the knob, no decay.
    float pass_gain = 1.0f;
    if(ctx.repeats < 0.97f)
    {
        float decay = sr * powf(30.0f, ctx.repeats / 0.97f); // 1 s to 30 s
        pass_gain   = powf(0.001f, length / rate / decay);
    }

    if(ctx.onset && ctx.onsets->Count() > 0)
    {
        pending_       = true;
        pending_start_ = ctx.onsets->Get(0) + (uint32_t)(kTunnelSkipS * sr);
        if(ctx.variation == 3)
            squeeze_ = 1.0f;
    }
    // Wait until the whole loop has been recorded.
    if(pending_ && (float)(int32_t)(ctx.write_pos - pending_start_) > length + 2.0f)
    {
        StartDrone(ctx, length);
        pending_ = false;
    }

    squeeze_ -= squeeze_ * ctx.block_frames / (0.8f * sr);
    float lfo_inc   = 0.3f * ctx.block_frames / sr;               // A: 0.3 Hz
    float sweep_inc = ctx.block_frames / (2.0f * ctx.beat_frames); // B: every 2 beats

    for(auto &d : drones_)
    {
        if(!ctx.voices->Active(d.voice))
            continue;
        ctx.voices->SetPassGain(d.voice, pass_gain);
        switch(ctx.variation)
        {
            case 0:
                d.phase = WrapPhase(d.phase + lfo_inc);
                ctx.voices->SetLength(d.voice, length * (1.0f + 0.5f * a * FastSin(d.phase)));
                break;
            case 1:
            {
                d.phase      = WrapPhase(d.phase + sweep_inc);
                float cutoff = 800.0f * powf(8.0f, a * FastSin(d.phase)); // 100 Hz to 6.4 kHz
                ctx.voices->SetFilter(d.voice, cutoff, 1.5f);
                break;
            }
            case 2:
                ctx.voices->SetFilter(d.voice, d.centre, 1.0f + 15.0f * a);
                break;
            default:
                ctx.voices->SetLength(d.voice, length * (1.0f - 0.7f * a * squeeze_));
                break;
        }
    }
}

// ---------------------------------------------------------------- Strum

namespace
{
// A, C, D: notes per step across Activity.
const float kStrumRates[4] = {0.25f, 0.5f, 1.0f, 2.0f};
} // namespace

void FxStrum::Reset()
{
    last_tick_ = -1;
    chain_     = 0;
    level_     = 0.0f;
}

bool FxStrum::Note(const EffectContext &ctx, int i, uint32_t *start, float *recorded) const
{
    if(i >= ctx.onsets->Count())
        return false;
    *start    = ctx.onsets->Get(i);
    *recorded = (float)(int32_t)(ctx.write_pos - *start) - 2.0f;
    return *recorded > 0.02f * ctx.sample_rate
           && *recorded < 0.7f * (float)ctx.buffer_frames;
}

void FxStrum::Play(const EffectContext &ctx,
                   uint32_t             start,
                   float                length,
                   float                rate,
                   float                gain,
                   float                pan)
{
    VoiceParams p;
    p.start   = start;
    p.length  = length;
    p.rate    = rate;
    p.reverse = ctx.reverse;
    p.env     = ctx.shape;
    p.soft    = 0.05f;
    p.gain    = gain;
    p.pan     = pan;
    ctx.voices->Start(p);
}

void FxStrum::Block(const EffectContext &ctx)
{
    if(ctx.onset)
        level_ = 1.0f;

    float a        = ctx.activity;
    int   density  = a < 0.25f ? 0 : (a < 0.5f ? 1 : (a < 0.8f ? 2 : 3));
    float per_step = ctx.variation == 1 ? 1.0f : kStrumRates[density];

    int32_t tick = (int32_t)floorf(ctx.step_pos * per_step);
    if(tick == last_tick_)
        return;
    last_tick_ = tick;

    uint32_t start;
    float    recorded;
    if(level_ < 0.001f || !Note(ctx, 0, &start, &recorded))
        return;

    float sr      = ctx.sample_rate;
    float length  = Clamp(ctx.step_frames, 0.08f * sr, 0.6f * sr);
    float spacing = ctx.step_frames / per_step;

    // Notes ring past the next one when they're longer than the spacing.
    float overlap = length / spacing;
    float gain    = (overlap > 1.0f ? 0.8f / sqrtf(overlap) : 0.8f) * level_;

    // Fade the repeats to -60 dB over the decay time.
    if(ctx.repeats < 0.97f)
    {
        float decay = 0.3f * sr * powf(40.0f, ctx.repeats / 0.97f); // 0.3 s to 12 s
        level_ *= powf(0.001f, spacing / decay);
    }

    switch(ctx.variation)
    {
        case 0:
            Play(ctx, start, fminf(length, recorded), 1.0f, gain,
                 0.5f + (ctx.rng->Uni() - 0.5f) * 0.6f);
            break;

        case 1:
        {
            // Copies a few ms apart and a few cents apart: they comb and
            // drift against each other.
            int   n      = 2 + (int)(a * 4.99f);
            float offset = (0.003f + 0.008f * a) * sr;
            float len    = fminf(length, recorded);
            for(int c = 0; c < n; c++)
            {
                float spread = (float)c - 0.5f * (float)(n - 1);
                Play(ctx, start - (uint32_t)(c * offset), len,
                     1.0f + 0.003f * spread, 1.2f / n,
                     0.5f + 0.6f * spread / (float)n);
            }
            break;
        }

        default:
        {
            // Oldest to newest of the last few notes, round and round.
            int n = 2 + (int)(a * 5.0f);
            if(n > ctx.onsets->Count())
                n = ctx.onsets->Count();
            if(n > 0)
            {
                int i = chain_ % n;
                chain_++;
                if(Note(ctx, n - 1 - i, &start, &recorded))
                    Play(ctx, start, fminf(length, recorded), 1.0f, gain,
                         0.2f + 0.6f * (float)i / (float)(n > 1 ? n - 1 : 1));
            }

            if(ctx.variation == 3 && ctx.rng->Uni() < 0.3f + 0.4f * a)
            {
                int j = (int)(ctx.rng->Uni() * (float)(n > 0 ? n : 1));
                if(Note(ctx, j, &start, &recorded))
                {
                    float len = fminf(length, recorded) * 0.5f;
                    Play(ctx, start, len, 2.0f, gain * 0.6f, ctx.rng->Uni());
                }
            }
            break;
        }
    }
}

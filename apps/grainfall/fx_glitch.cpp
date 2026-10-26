#include "fx_glitch.h"

#include <cmath>

using namespace dsp;

namespace
{
// Where the i-th most recent note starts, and how much of it is recorded
// so far. False if there's none, or it's about to be recorded over.
bool RecentNote(const EffectContext &ctx, int i, uint32_t *start, float *recorded)
{
    if(i >= ctx.onsets->Count())
        return false;
    *start    = ctx.onsets->Get(i);
    *recorded = (float)(int32_t)(ctx.write_pos - *start) - 2.0f;
    return *recorded > 0.02f * ctx.sample_rate
           && *recorded < 0.7f * (float)ctx.buffer_frames;
}

VoiceParams Slice(const EffectContext &ctx, uint32_t start, float length, float rate)
{
    VoiceParams p;
    p.start   = start;
    p.length  = length;
    p.rate    = rate;
    p.reverse = ctx.reverse;
    p.env     = ctx.shape;
    p.soft    = 0.03f;
    p.gain    = 0.85f;
    p.pan     = 0.5f + (ctx.rng->Uni() - 0.5f) * 0.6f;
    return p;
}

// A random pick from a short table.
template <int N>
float Pick(dsp::Rng &rng, const float (&table)[N])
{
    int i = (int)(rng.Uni() * (float)N);
    return table[i < N ? i : N - 1];
}

const float kOctaveFifth[] = {0.5f, 1.4983f, 2.0f};
const float kShifts[]      = {0.5f, 0.75f, 1.4983f, 2.0f};
const float kDrastic[]     = {0.25f, 0.5f, 2.0f, 4.0f};
} // namespace

// ---------------------------------------------------------------- Blocks

namespace
{
// Blocks back from the anchor, before Activity reshuffles them: mostly the
// newest block (the note that set the burst off), stepping back now and
// then.
const int kBlocksBase[8] = {0, 1, 0, 0, 2, 1, 0, 3};
} // namespace

void FxBlocks::Reset()
{
    pos_          = 0;
    left_         = 0;
    last_tick_    = -1;
    last_quarter_ = -1;
    run_left_     = 0;
    anchor_       = 0;
    note_         = 0;
    first_        = false;
}

void FxBlocks::PlayBlock(const EffectContext &ctx, float tick, bool first)
{
    dsp::Rng &rng = *ctx.rng;
    float     a   = ctx.activity;

    // A: sometimes a run of recent notes takes the tick instead.
    if(ctx.variation == 0 && ctx.onsets->Count() >= 2 && rng.Uni() < 0.6f * a)
    {
        run_left_ = kRun;
        return;
    }

    int   back = back_[pos_];
    float rate = 1.0f;
    float len  = tick;
    switch(ctx.variation)
    {
        case 1:
            if(rng.Uni() < a)
                rate = Pick(rng, kOctaveFifth);
            break;
        case 2: len = 2.0f * tick; break;
        case 3:
            if(rng.Uni() < a)
                rate = Pick(rng, kShifts);
            break;
        default: break;
    }

    // `len` of output (a tick, C two) at `rate`, ending where the block
    // ends, so even a reversed block reads only what's recorded. Except the
    // burst's first: it starts at the attack, which is still being recorded,
    // so it plays forwards at no more than normal speed to stay behind the
    // write head.
    bool     live  = first && back == 0;
    uint32_t start = anchor_ - (uint32_t)((float)back * tick + len * rate) - 2;
    if(live)
    {
        rate  = fminf(rate, 1.0f);
        start = note_;
    }
    VoiceParams p = Slice(ctx, start, len * rate, rate);

    if(live)
        p.reverse = false;
    else if(rng.Uni() < 0.25f * a)
        p.reverse = !p.reverse;
    if(ctx.variation != 2 && rng.Uni() < 0.35f * a)
    {
        // Stutter: the block's start, two or four times over.
        p.passes = rng.Uni() < 0.5f ? 2 : 4;
        p.length /= (float)p.passes;
    }

    if(ctx.variation == 2)
    {
        p.env    = Env::kRampDown;
        p.soft   = 0.1f;
        p.gain   = 0.6f;
        p.filter = VoiceFilter::kLowPass;
        p.cutoff = 5000.0f * powf(0.08f, (0.3f + 0.7f * a) * rng.Uni()); // down to 400 Hz
        p.q      = 0.9f;
    }
    else if(ctx.variation == 3)
    {
        p.crush_bits = 10 - (int)(6.0f * a); // 10 to 4 bits
        p.crush_hold = 1 + (int)(5.0f * a);
        p.gain       = 0.7f;
    }
    ctx.voices->Start(p);
}

void FxBlocks::Block(const EffectContext &ctx)
{
    float sr   = ctx.sample_rate;
    float tick = Clamp(0.5f * ctx.step_frames, 0.04f * sr, 1.0f * sr);

    if(ctx.onset)
    {
        // Burst length in ticks: 2 to 32, endless at the top.
        int ticks = ctx.repeats >= 0.97f
                        ? -1
                        : (int)(2.0f * powf(16.0f, ctx.repeats / 0.97f) + 0.5f);
        if(left_ == 0)
        {
            pos_   = 0;
            first_ = true;
            note_  = ctx.onsets->Get(0);
        }
        left_ = ticks;
    }

    // A: a run plays a quarter of a tick per note, oldest to newest.
    int32_t quarter = (int32_t)floorf(ctx.step_pos * 8.0f);
    if(quarter != last_quarter_)
    {
        last_quarter_ = quarter;
        if(run_left_ > 0)
        {
            run_left_--;
            int      n = ctx.onsets->Count() < kRun ? ctx.onsets->Count() : kRun;
            uint32_t start;
            float    recorded;
            if(n > 0 && RecentNote(ctx, run_left_ % n, &start, &recorded))
            {
                VoiceParams p = Slice(ctx, start, fminf(0.25f * tick, recorded), 1.0f);
                p.gain        = 0.75f;
                p.pan         = 0.2f + 0.6f * (float)(run_left_ % n) / (float)n;
                ctx.voices->Start(p);
            }
        }
    }

    int32_t t = (int32_t)floorf(ctx.step_pos * 2.0f);
    if(t == last_tick_)
        return;
    last_tick_ = t;
    if(left_ == 0 || run_left_ > 0)
        return;
    if(left_ > 0)
        left_--;

    // Every four ticks the blocks are taken from the two steps just played.
    // The burst's first four end a tick after the attack, so it opens with
    // the note.
    if(pos_ % 4 == 0)
    {
        anchor_ = first_ ? note_ + (uint32_t)tick : ctx.write_pos;
        for(int i = pos_; i < pos_ + 4; i++)
        {
            back_[i] = kBlocksBase[i];
            if(ctx.rng->Uni() < ctx.activity)
                back_[i] = (int)(ctx.rng->Uni() * 3.99f);
        }
    }
    PlayBlock(ctx, tick, first_);
    first_ = false;
    pos_   = (pos_ + 1) % kPattern;
}

// ---------------------------------------------------------------- Interrupt

void FxInterrupt::Reset()
{
    last_step_   = -1;
    left_        = 0.0f;
    pos_         = 0.0f;
    sweep_       = 0;
    duck_        = 0.0f;
    duck_target_ = 0.0f;
}

void FxInterrupt::Chop(const EffectContext &ctx)
{
    dsp::Rng &rng  = *ctx.rng;
    float     a    = ctx.activity;
    float     rate = 1.0f;
    switch(ctx.variation)
    {
        case 1:
            if(rng.Uni() < 0.3f + 0.7f * a)
                rate = Pick(rng, kShifts);
            break;
        case 3:
            if(rng.Uni() < 0.3f + 0.7f * a)
                rate = Pick(rng, kDrastic);
            break;
        default: break;
    }

    // From anywhere in the last two glitch lengths.
    float       back = rng.Uni() * 2.0f * length_;
    VoiceParams p    = Slice(ctx, ctx.SliceStart(chop_ * rate, back), chop_ * rate, rate);
    p.gain           = 0.9f;

    switch(ctx.variation)
    {
        case 0:
            if(rng.Uni() < 0.5f * a)
                p.reverse = !p.reverse;
            break;
        case 2:
        {
            // Echoes: the slice repeats, each time quieter, through a
            // band-pass that falls across the glitch.
            p.passes    = 2 + (int)(a * 2.99f);
            p.length    = chop_ / (float)p.passes;
            p.pass_gain = 0.6f;
            p.filter    = VoiceFilter::kBandPass;
            p.cutoff    = 4000.0f;
            p.q         = 3.0f;
            p.gain      = 1.6f; // make up what the band-pass takes away
            p.pan       = 0.5f;
            break;
        }
        case 3:
            p.crush_bits = 6 - (int)(2.0f * a); // 6 to 4 bits
            p.crush_hold = 2 + (int)(6.0f * a);
            p.gain       = 0.7f;
            if(rng.Uni() < a)
            {
                p.passes = 4;
                p.length /= 4.0f;
            }
            break;
        default: break;
    }

    VoicePool::Handle h = ctx.voices->Start(p);
    if(ctx.variation == 2)
        sweep_ = h;
    next_chop_ += chop_;
}

void FxInterrupt::Start(const EffectContext &ctx)
{
    float sr = ctx.sample_rate;
    float a  = ctx.activity;

    length_ = ctx.step_frames;
    if(ctx.rng->Uni() < 0.3f * a)
        length_ *= ctx.rng->Uni() < 0.5f ? 0.5f : 2.0f;
    length_ = Clamp(length_, 0.06f * sr, 1.5f * sr);

    int chops;
    switch(ctx.variation)
    {
        case 2: chops = 1; break;
        case 3: chops = 2 + (int)(a * 6.99f); break; // 2 to 8
        default: chops = 1 + (int)(a * 3.99f); break; // 1 to 4
    }
    chop_        = length_ / (float)chops;
    left_        = length_;
    pos_         = 0.0f;
    next_chop_   = 0.0f;
    duck_target_ = 1.0f;
    Chop(ctx);
}

void FxInterrupt::Block(const EffectContext &ctx)
{
    if(duck_coef_ == 0.0f)
        duck_coef_ = SmoothCoef(0.003f, ctx.sample_rate);
    through_ = ctx.through;

    if(left_ > 0.0f)
    {
        pos_ += (float)ctx.block_frames;
        left_ -= (float)ctx.block_frames;
        if(left_ <= 0.0f)
            duck_target_ = 0.0f;
        else if(pos_ >= next_chop_)
            Chop(ctx);
        if(ctx.variation == 2)
            ctx.voices->SetFilter(sweep_, 4000.0f * powf(0.075f, pos_ / length_), 3.0f);
    }

    // Repeats: the chance of a glitch on each step, 4% to every step. A
    // glitch a step long is counted down in blocks, so it can have a few
    // frames left when the next step comes; one with less than half a step
    // to go gives way.
    if(ctx.step && left_ < 0.5f * ctx.step_frames
       && ctx.rng->Uni() < 0.04f + 0.96f * ctx.repeats * ctx.repeats)
        Start(ctx);
}

void FxInterrupt::Process(const SampleBuffer &,
                          float  in_l,
                          float  in_r,
                          float *l,
                          float *r,
                          float *,
                          float *)
{
    // The live signal passes through, except while a glitch plays.
    duck_ += duck_coef_ * (duck_target_ - duck_);
    float pass = through_ * (1.0f - duck_);
    *l += in_l * pass;
    *r += in_r * pass;
}

// ---------------------------------------------------------------- Arp

namespace
{
// B: up and down a major arpeggio, an octave and a third.
const float kArpRates[8] = {1.0f, 1.2599f, 1.4983f, 2.0f, 2.5198f, 2.0f, 1.4983f, 1.2599f};
} // namespace

void FxArp::Reset()
{
    pos_   = 0;
    level_ = 0.0f;
}

void FxArp::Roll(const EffectContext &ctx)
{
    dsp::Rng &rng = *ctx.rng;
    for(int i = 0; i < kMaxSteps; i++)
    {
        cutoff_[i] = 300.0f * powf(20.0f, rng.Uni()); // 300 Hz to 6 kHz
        q_[i]      = 0.7f + 1.8f * rng.Uni();
        band_[i]   = rng.Uni() < 0.5f;
        bits_[i]   = 4 + (int)(rng.Uni() * 4.99f); // 4 to 8
        hold_[i]   = 1 + (int)(rng.Uni() * 5.99f);
    }
}

void FxArp::Block(const EffectContext &ctx)
{
    if(ctx.onset)
    {
        level_ = 1.0f;
        Roll(ctx);
    }

    if(!ctx.step)
        return;

    int steps = 2 + (int)(ctx.activity * 6.99f);
    int n     = ctx.onsets->Count() < steps ? ctx.onsets->Count() : steps;
    if(n == 0 || level_ < 0.001f)
        return;
    int i = pos_ % steps;
    pos_  = (pos_ + 1) % (steps * kMaxSteps);

    // The recent notes oldest to newest, round again to fill the steps.
    uint32_t start;
    float    recorded;
    if(!RecentNote(ctx, n - 1 - i % n, &start, &recorded))
        return;

    float sr      = ctx.sample_rate;
    float spacing = ctx.step_frames;
    float len     = Clamp(spacing, 0.04f * sr, 0.5f * sr);
    float rate    = ctx.variation == 1 ? kArpRates[i] : 1.0f;

    VoiceParams p = Slice(ctx, start, fminf(len * rate, recorded), rate);
    p.gain        = 0.8f * level_;
    p.pan         = 0.25f + 0.5f * (float)i / (float)(steps - 1);
    if(ctx.variation == 2)
    {
        p.filter = band_[i] ? VoiceFilter::kBandPass : VoiceFilter::kLowPass;
        p.cutoff = cutoff_[i];
        p.q      = q_[i];
        p.gain *= band_[i] ? 1.3f : 0.8f; // even out the band-pass's loss and the resonance
    }
    else if(ctx.variation == 3)
    {
        p.crush_bits = bits_[i];
        p.crush_hold = hold_[i];
        p.gain *= 0.8f;
    }
    ctx.voices->Start(p);

    // Fade to -60 dB over the decay time, as Strum does.
    if(ctx.repeats < 0.97f)
    {
        float decay = 0.3f * sr * powf(40.0f, ctx.repeats / 0.97f); // 0.3 s to 12 s
        level_ *= powf(0.001f, spacing / decay);
    }
}

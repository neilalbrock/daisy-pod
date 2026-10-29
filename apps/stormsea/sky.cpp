#include "sky.h"

#include <cmath>

using namespace dsp;

namespace
{
// Rain: drops a second at full rain (they grow with its square, so a
// little rain is a few drops), their lengths, how far back they reach, and
// the pitches they pick from.
constexpr float kMaxDrops     = 250.0f;
constexpr float kMinDrops     = 2.0f;
constexpr float kDropShortest = 0.002f; // s
constexpr float kDropLongest  = 0.02f;
constexpr float kDropNearest  = 0.05f; // s back from now
constexpr float kDropFurthest = 0.5f;
constexpr float kDropRates[]  = {1.0f, 1.5f, 2.0f};
constexpr float kDropHighPass = 600.0f; // Hz

// Lightning: strikes a second at full lightning (more in the gusts), the
// slice's length, the attack, and the treble boost: the slice plus this
// much again of what's above the corner. A high-pass alone left a dark
// tail's strike with next to nothing.
constexpr float kMaxStrikes     = 0.5f;
constexpr float kGustStrikes    = 2.0f; // a full gust triples the rate
constexpr float kSliceShortest  = 0.15f; // s
constexpr float kSliceLongest   = 0.5f;
constexpr float kStrikeAttack   = 0.003f; // s
constexpr float kStrikeCorner   = 1000.0f; // Hz
constexpr float kStrikeTreble   = 2.0f;
constexpr float kStrikeDecay    = -8.66f; // log2 of the level at the end: about -52 dB

// Thunder: how long after its strike, and its low-pass, near and far.
constexpr float kThunderSoonest = 0.2f; // s
constexpr float kThunderLatest  = 3.0f;
constexpr float kThunderNear    = 300.0f; // Hz
constexpr float kThunderFar     = 120.0f;
constexpr float kThunderSwell   = 0.35f; // of its length, swelling in
constexpr float kSwellIn        = 0.5f / kThunderSwell; // the envelope's half cycles a unit of t
constexpr float kSwellOut       = 0.5f / (1.0f - kThunderSwell);

// Each one's level, into the sea and heard.
constexpr float kDropGain     = 1.5f;
constexpr float kStrikeGain   = 4.0f;
constexpr float kThunderGain  = 5.0f;
constexpr float kRainIn       = 0.35f, kRainOut = 1.0f;
constexpr float kStrikeIn     = 0.4f, kStrikeOut = 1.0f;
constexpr float kThunderIn    = 0.6f, kThunderOut = 1.0f;

float OnePole(float hz, float sample_rate)
{
    return 1.0f - expf(-kTwoPi * hz / sample_rate);
}

// Equal power: the left and right gains for a position, 0 left .. 1 right.
void Pan(float gain, float pan, float *l, float *r)
{
    *l = gain * FastSin(0.25f * (1.0f - pan)); // sin(pi / 2 (1 - pan))
    *r = gain * FastSin(0.25f * pan);
}
} // namespace

void Sky::Init(float sample_rate, size_t block_size, float *mem)
{
    sample_rate_ = sample_rate;
    block_rate_  = sample_rate / (float)block_size;
    ring_        = mem;
    for(size_t i = 0; i < kFloats; i++)
        ring_[i] = 0.0f;
    pos_ = 0;
    rng_.Seed(0x5C1E5u);
    drop_hp_     = OnePole(kDropHighPass, sample_rate);
    strike_hp_   = OnePole(kStrikeCorner, sample_rate);
    attack_step_ = 1.0f / (kStrikeAttack * sample_rate);
    flash_decay_ = expf(-1.0f / (0.06f * block_rate_));
    for(int i = 0; i < kBolts; i++)
        rumble_[i].Init(0.04f, 0.15f, block_rate_, 0xB00Bu + 31u * i);
}

Sky::Voice *Sky::Free(Voice *voices, int n)
{
    for(int i = 0; i < n; i++)
        if(voices[i].left <= 0)
            return &voices[i];
    return nullptr;
}

void Sky::StartStrike(int strike)
{
    Voice *v = Free(strikes_, kBolts);
    if(!v)
        return;
    float frames = rng_.Range(kSliceShortest, kSliceLongest) * sample_rate_;
    // The slice ends a little before now, so a Flash, reading at twice
    // the speed, never catches up with what's being written.
    float start = (float)pos_ - frames - 64.0f;
    if(start < 0.0f)
        start += (float)kRingFrames;
    if(strike == kFork)
        strike = rng_.Uni() < 0.5f ? kFlash : kGlint;

    *v = Voice{};
    if(strike == kFlash)
    {
        v->pos    = start;
        v->rate   = 2.0f;
        v->length = (int)(0.5f * frames);
    }
    else
    {
        v->pos  = start + frames;
        v->rate = -1.0f;
        v->length = (int)frames;
        if(v->pos >= (float)kRingFrames)
            v->pos -= (float)kRingFrames;
    }
    v->left     = v->length;
    v->env_step = 1.0f / (float)v->length;
    v->coef     = strike_hp_;
    Pan(kStrikeGain, rng_.Range(0.2f, 0.8f), &v->gain_l, &v->gain_r);
    flash_ = 1.0f;

    // Its thunder, the further off the later.
    if(thunder_amount_ <= 0.0f)
        return;
    int i = 0;
    while(i < kBolts && thunder_[i].left > 0)
        i++;
    if(i == kBolts)
        return;
    Voice &t    = thunder_[i];
    float delay = rng_.Range(kThunderSoonest, kThunderLatest);
    float far   = (delay - kThunderSoonest) / (kThunderLatest - kThunderSoonest);
    t           = Voice{};
    t.wait      = (int)(delay * sample_rate_);
    t.pos       = start;
    t.rate      = 0.5f;
    t.length    = (int)(2.0f * frames);
    t.left      = t.length;
    t.env_step  = 1.0f / (float)t.length;
    t.coef      = OnePole(kThunderNear * powf(kThunderFar / kThunderNear, far), sample_rate_);
    Pan(kThunderGain * thunder_amount_ * (1.0f - 0.7f * far), rng_.Range(0.35f, 0.65f), &t.gain_l, &t.gain_r);
}

void Sky::Set(float rain, float lightning, float thunder, int strike, float gust)
{
    rain_           = Clamp(rain, 0.0f, 1.0f);
    thunder_amount_ = Clamp(thunder, 0.0f, 1.0f);
    flash_ *= flash_decay_;

    // A drop or a strike starts in this block with the chance its rate
    // gives: both are far below one a block.
    rain_rate_ = rain_ > 0.0f ? kMinDrops + (kMaxDrops - kMinDrops) * rain_ * rain_ : 0.0f;
    if(rain_rate_ > 0.0f && rng_.Uni() * block_rate_ < rain_rate_)
    {
        Voice *v = Free(drops_, kDrops);
        if(v)
        {
            float back = rng_.Range(kDropNearest, kDropFurthest) * sample_rate_;
            *v         = Voice{};
            v->pos     = (float)pos_ - back;
            if(v->pos < 0.0f)
                v->pos += (float)kRingFrames;
            v->rate     = kDropRates[rng_.Next() % 3];
            v->length   = (int)(rng_.Range(kDropShortest, kDropLongest) * sample_rate_);
            v->left     = v->length;
            v->env_step = 1.0f / (float)v->length;
            v->coef     = drop_hp_;
            Pan(kDropGain * rain_ * rng_.Range(0.3f, 1.0f), rng_.Uni(), &v->gain_l, &v->gain_r);
        }
    }

    float l      = Clamp(lightning, 0.0f, 1.0f);
    strike_rate_ = kMaxStrikes * l * (1.0f + kGustStrikes * Clamp(gust, 0.0f, 1.0f));
    if(strike_rate_ > 0.0f && rng_.Uni() * block_rate_ < strike_rate_)
        StartStrike(strike);

    for(int i = 0; i < kBolts; i++)
        if(thunder_[i].left > 0 && thunder_[i].wait <= 0)
            roll_[i] = 0.65f + 0.35f * rumble_[i].Next();
}

float Sky::Read(float pos) const
{
    uint32_t i = (uint32_t)pos;
    float    f = pos - (float)i;
    uint32_t j = (i + 1) & (kRingFrames - 1);
    float    a = ring_[2 * i] + ring_[2 * i + 1];
    float    b = ring_[2 * j] + ring_[2 * j + 1];
    return 0.5f * (a + f * (b - a));
}

void Sky::Render(float *into_l, float *into_r, float *out_l, float *out_r)
{
    float in_l = 0.0f, in_r = 0.0f, o_l = 0.0f, o_r = 0.0f;
    const float ring = (float)kRingFrames;

    // Rain: Hann-shaped drops through a high-pass.
    for(Voice &v : drops_)
    {
        if(v.left <= 0)
            continue;
        float x = Read(v.pos);
        v.lp1 += v.coef * (x - v.lp1);
        float env = 0.5f - 0.5f * FastSin(v.t + 0.25f); // (1 - cos(2 pi t)) / 2
        x         = env * (x - v.lp1);
        in_l += kRainIn * v.gain_l * x;
        in_r += kRainIn * v.gain_r * x;
        o_l += kRainOut * v.gain_l * x;
        o_r += kRainOut * v.gain_r * x;
        v.t += v.env_step;
        v.pos += v.rate;
        if(v.pos >= ring)
            v.pos -= ring;
        v.left--;
    }

    // Lightning: a sharp attack, then dying away, with its top lifted.
    for(Voice &v : strikes_)
    {
        if(v.left <= 0)
            continue;
        float x = Read(v.pos);
        v.lp1 += v.coef * (x - v.lp1);
        float attack = fminf((float)(v.length - v.left) * attack_step_, 1.0f);
        x            = attack * FastExp2(kStrikeDecay * v.t) * (x + kStrikeTreble * (x - v.lp1));
        in_l += kStrikeIn * v.gain_l * x;
        in_r += kStrikeIn * v.gain_r * x;
        o_l += kStrikeOut * v.gain_l * x;
        o_r += kStrikeOut * v.gain_r * x;
        v.t += v.env_step;
        v.pos += v.rate;
        if(v.pos < 0.0f)
            v.pos += ring;
        else if(v.pos >= ring)
            v.pos -= ring;
        v.left--;
    }

    // Thunder: waits, then swells in and rolls away through two low-passes.
    for(int i = 0; i < kBolts; i++)
    {
        Voice &v = thunder_[i];
        if(v.left <= 0)
            continue;
        if(v.wait > 0)
        {
            v.wait--;
            continue;
        }
        float x = Read(v.pos);
        v.lp1 += v.coef * (x - v.lp1);
        v.lp2 += v.coef * (v.lp1 - v.lp2);
        // Half a cosine up, then half a cosine down.
        float env = v.t < kThunderSwell
                        ? 0.5f - 0.5f * FastSin(kSwellIn * v.t + 0.25f)
                        : 0.5f + 0.5f * FastSin(kSwellOut * (v.t - kThunderSwell) + 0.25f);
        x = env * roll_[i] * v.lp2;
        in_l += kThunderIn * v.gain_l * x;
        in_r += kThunderIn * v.gain_r * x;
        o_l += kThunderOut * v.gain_l * x;
        o_r += kThunderOut * v.gain_r * x;
        v.t += v.env_step;
        v.pos += v.rate;
        if(v.pos >= ring)
            v.pos -= ring;
        v.left--;
    }

    *into_l = in_l;
    *into_r = in_r;
    *out_l  = o_l;
    *out_r  = o_r;
}

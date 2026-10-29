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

// Lightning: strikes a second at full lightning (more in the gusts).
constexpr float kMaxStrikes  = 0.5f;
constexpr float kGustStrikes = 2.0f; // a full gust triples the rate

// Thunder: its delay at Distance 0 and 1 (each strike's varies by up to
// kDelaySpread either way), and what the delay brings, near to far: the
// slice of tail it plays, its low-pass, the share of it swelling in, and
// its level (far is 5 dB down, and darker). Reversed thunder swells in
// over most of it and stops short.
constexpr float kThunderSoonest = 0.15f; // s
constexpr float kThunderLatest  = 5.0f;
constexpr float kDelaySpread    = 0.3f;
constexpr float kSliceNear      = 0.12f; // s
constexpr float kSliceFar       = 0.7f; // two octaves down plays it 4x as long: the ring's 11 s holds 9.9 s back
constexpr float kThunderNear    = 900.0f; // Hz
constexpr float kThunderFar     = 120.0f;
constexpr float kSwellNear      = 0.06f;
constexpr float kSwellFar       = 0.4f;
constexpr float kSwellReverse   = 0.8f;
constexpr float kFarLevel       = 0.56f;
constexpr float kRumbleRates[Sky::kNumRumbles] = {0.5f, 0.25f, -0.5f};
constexpr float kRumbleLevels[Sky::kNumRumbles] = {1.0f, 0.7f, 1.0f}; // two octaves down keeps more under the low-pass

// Each one's level, into the sea and heard.
constexpr float kDropGain    = 1.5f;
constexpr float kThunderGain = 5.0f;
constexpr float kRainIn      = 0.35f, kRainOut = 1.0f;
constexpr float kThunderIn   = 0.6f, kThunderOut = 1.0f;

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
    flash_decay_ = expf(-1.0f / (0.06f * block_rate_));
    for(int i = 0; i < kThunders; i++)
        rumble_[i].Init(0.04f, 0.15f, block_rate_, 0xB00Bu + 31u * i);
}

Sky::Voice *Sky::Free(Voice *voices, int n)
{
    for(int i = 0; i < n; i++)
        if(voices[i].left <= 0)
            return &voices[i];
    return nullptr;
}

void Sky::Strike(int rumble)
{
    flash_ = 1.0f;
    Voice *t = Free(thunder_, kThunders);
    if(!t)
        return;
    rumble = rumble < 0 ? 0 : (rumble >= kNumRumbles ? kNumRumbles - 1 : rumble);

    // How far off this one is: Distance's delay, varied, then what the
    // delay brings with it.
    float delay = kThunderSoonest * powf(kThunderLatest / kThunderSoonest, distance_);
    delay *= rng_.Range(1.0f - kDelaySpread, 1.0f + kDelaySpread);
    float far = Clamp(logf(delay / kThunderSoonest) / logf(kThunderLatest / kThunderSoonest), 0.0f, 1.0f);

    // The slice of tail as it is now, ending a little before the write.
    float frames = Lerp(kSliceNear, kSliceFar, far) * rng_.Range(0.8f, 1.2f) * sample_rate_;
    float start  = (float)pos_ - frames - 64.0f;
    if(start < 0.0f)
        start += (float)kRingFrames;

    float rate = kRumbleRates[rumble];
    *t         = Voice{};
    t->wait    = (int)(delay * sample_rate_);
    t->rate    = rate;
    t->pos     = rate < 0.0f ? start + frames : start;
    if(t->pos >= (float)kRingFrames)
        t->pos -= (float)kRingFrames;
    t->length    = (int)(frames / fabsf(rate));
    t->left      = t->length;
    t->env_step  = 1.0f / (float)t->length;
    t->coef      = OnePole(kThunderNear * powf(kThunderFar / kThunderNear, far), sample_rate_);
    t->swell     = rumble == kReverse ? kSwellReverse : Lerp(kSwellNear, kSwellFar, far);
    t->swell_in  = 0.5f / t->swell;
    t->swell_out = 0.5f / (1.0f - t->swell);
    float level  = kThunderGain * kRumbleLevels[rumble] * Lerp(1.0f, kFarLevel, far);
    Pan(level, rng_.Range(0.3f, 0.7f), &t->gain_l, &t->gain_r);
}

void Sky::Set(float rain, float lightning, float distance, int rumble, float gust)
{
    rain_     = Clamp(rain, 0.0f, 1.0f);
    distance_ = Clamp(distance, 0.0f, 1.0f);
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
        Strike(rumble);

    rolling_ = 0.0f;
    for(int i = 0; i < kThunders; i++)
    {
        const Voice &v = thunder_[i];
        if(v.left <= 0 || v.wait > 0)
            continue;
        roll_[i] = 0.65f + 0.35f * rumble_[i].Next();
        float env = v.t < v.swell ? v.t / v.swell : (1.0f - v.t) / (1.0f - v.swell);
        rolling_ += env * roll_[i] * (v.gain_l + v.gain_r) * (0.5f / kThunderGain);
    }
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

    // Thunder: waits, then swells in and rolls away through two low-passes.
    for(int i = 0; i < kThunders; i++)
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
        float env = v.t < v.swell ? 0.5f - 0.5f * FastSin(v.swell_in * v.t + 0.25f)
                                  : 0.5f + 0.5f * FastSin(v.swell_out * (v.t - v.swell) + 0.25f);
        x = env * roll_[i] * v.lp2;
        in_l += kThunderIn * v.gain_l * x;
        in_r += kThunderIn * v.gain_r * x;
        o_l += kThunderOut * v.gain_l * x;
        o_r += kThunderOut * v.gain_r * x;
        v.t += v.env_step;
        v.pos += v.rate;
        if(v.pos < 0.0f)
            v.pos += ring;
        else if(v.pos >= ring)
            v.pos -= ring;
        v.left--;
    }

    *into_l = in_l;
    *into_r = in_r;
    *out_l  = o_l;
    *out_r  = o_r;
}

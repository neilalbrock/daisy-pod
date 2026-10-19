#include "grain_engine.h"

#include <cmath>

namespace
{
constexpr float  kTwoPi      = 6.28318530718f;
constexpr size_t kWindowSize = 512;

// Hann window, one extra point so interpolation never wraps. Filled in Init().
float window[kWindowSize + 1];

float Clamp(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}
} // namespace

void GrainEngine::Init(float  sample_rate,
                       float *buf_l,
                       float *buf_r,
                       size_t buffer_len)
{
    for(size_t i = 0; i <= kWindowSize; i++)
        window[i] = 0.5f - 0.5f * cosf(kTwoPi * (float)i / kWindowSize);

    sample_rate_ = sample_rate;
    buf_l_       = buf_l;
    buf_r_       = buf_r;
    len_         = buffer_len;
    write_       = 0;
    spawn_acc_   = 0.0f;
    for(size_t i = 0; i < len_; i++)
    {
        buf_l_[i] = 0.0f;
        buf_r_[i] = 0.0f;
    }
    for(auto &g : grains_)
        g.active = false;
    SetParams(Params());
}

void GrainEngine::SetParams(const Params &p)
{
    float max_s = (float)len_ / sample_rate_;

    params_            = p;
    params_.size_s     = Clamp(p.size_s, 0.005f, 1.0f);
    params_.density_hz = Clamp(p.density_hz, 0.05f, 200.0f);
    params_.pitch      = Clamp(p.pitch, 0.25f, 4.0f);
    params_.spread     = Clamp(p.spread, 0.0f, 1.0f);
    // Leave room for the longest grain read at the highest pitch.
    params_.position_s = Clamp(p.position_s, 0.0f, max_s * 0.5f);
    params_.spray_s    = Clamp(p.spray_s, 0.0f, max_s * 0.25f);

    // Grains overlap by about size * density on average. Scaling by the
    // square root keeps the level roughly steady (grains are uncorrelated).
    float overlap = params_.size_s * params_.density_hz;
    norm_         = overlap > 1.0f ? 1.0f / sqrtf(overlap) : 1.0f;
}

float GrainEngine::Random()
{
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return (float)(rng_ >> 8) * (1.0f / 16777216.0f);
}

float GrainEngine::Read(const float *buf, float pos) const
{
    size_t i0   = (size_t)pos;
    float  frac = pos - (float)i0;
    size_t i1   = i0 + 1 >= len_ ? 0 : i0 + 1;
    return buf[i0] + frac * (buf[i1] - buf[i0]);
}

void GrainEngine::StartGrain()
{
    Grain *g = nullptr;
    for(auto &candidate : grains_)
    {
        if(!candidate.active)
        {
            g = &candidate;
            break;
        }
    }
    if(g == nullptr)
        return; // pool full: drop this grain rather than steal a sounding one

    float length = params_.size_s * sample_rate_;

    // Start far enough back that a grain played faster than real time never
    // overtakes the record head and reads the discontinuity there.
    float delay = params_.position_s * sample_rate_
                  + Random() * params_.spray_s * sample_rate_;
    if(params_.pitch > 1.0f)
        delay += length * (params_.pitch - 1.0f);
    delay += 1.0f; // stay clear of the sample being written

    float start = (float)write_ - delay;
    while(start < 0.0f)
        start += (float)len_;

    // Equal-power pan, centre +/- spread.
    float pan = 0.5f + (Random() - 0.5f) * params_.spread;

    g->active    = true;
    g->pos       = start;
    g->inc       = params_.pitch;
    g->phase     = 0.0f;
    g->phase_inc = 1.0f / length;
    g->gain_l    = cosf(pan * kTwoPi * 0.25f);
    g->gain_r    = sinf(pan * kTwoPi * 0.25f);
}

void GrainEngine::Process(float in_l, float in_r, float *out_l, float *out_r)
{
    if(!params_.freeze)
    {
        buf_l_[write_] = in_l;
        buf_r_[write_] = in_r;
        if(++write_ >= len_)
            write_ = 0;
    }

    spawn_acc_ += params_.density_hz / sample_rate_;
    if(params_.ext_trigger)
        spawn_acc_ = 0.0f;
    else if(spawn_acc_ >= 1.0f)
    {
        spawn_acc_ -= 1.0f;
        StartGrain();
    }

    float sum_l = 0.0f, sum_r = 0.0f;
    for(auto &g : grains_)
    {
        if(!g.active)
            continue;

        float  wpos = g.phase * kWindowSize;
        size_t wi   = (size_t)wpos;
        float  w    = window[wi] + (wpos - wi) * (window[wi + 1] - window[wi]);
        sum_l += Read(buf_l_, g.pos) * w * g.gain_l;
        sum_r += Read(buf_r_, g.pos) * w * g.gain_r;

        g.pos += g.inc;
        if(g.pos >= (float)len_)
            g.pos -= (float)len_;
        g.phase += g.phase_inc;
        if(g.phase >= 1.0f)
            g.active = false;
    }

    // Pan gains are equal power, so a centred grain is 3 dB down; make that up.
    *out_l = sum_l * norm_ * 1.41421356f;
    *out_r = sum_r * norm_ * 1.41421356f;
}

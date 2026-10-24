#include "voice.h"

#include <cmath>

using namespace dsp;

namespace
{
constexpr float kFadeS    = 0.003f; // anti-click fade at each end of a pass
constexpr float kReleaseS = 0.008f;

float Shape(Env env, float p)
{
    switch(env)
    {
        case Env::kSquare: return 1.0f;
        case Env::kRampUp: return p;
        case Env::kTriangle: return 1.0f - fabsf(2.0f * p - 1.0f);
        case Env::kRampDown: return 1.0f - p;
        case Env::kHann:
        default:
        {
            float s = FastSin(p * 0.5f); // sin(pi p)
            return s * s;
        }
    }
}
} // namespace

float EnvPower(Env env, float soft)
{
    switch(env)
    {
        // Linear fades over `soft` at each end keep a third of the power.
        case Env::kSquare: return 1.0f - 4.0f * Clamp(soft, 0.0f, 0.5f) / 3.0f;
        case Env::kHann: return 0.375f;
        default: return 1.0f / 3.0f; // ramps and triangle
    }
}

void VoicePool::Init(float sample_rate)
{
    sample_rate_ = sample_rate;
    fade_frames_ = kFadeS * sample_rate;
    release_inc_ = 1.0f / (kReleaseS * sample_rate);
    for(auto &v : voices_)
        v.active = false;
}

VoicePool::Handle VoicePool::Start(const VoiceParams &p)
{
    int    free_index = -1;
    Voice *oldest     = nullptr;
    for(int i = 0; i < kMaxVoices; i++)
    {
        Voice &v = voices_[i];
        if(!v.active)
        {
            free_index = i;
            break;
        }
        if(!v.releasing && (oldest == nullptr || v.order < oldest->order))
            oldest = &v;
    }
    if(free_index < 0)
    {
        if(oldest != nullptr)
            oldest->releasing = true;
        return 0;
    }

    Voice &v    = voices_[free_index];
    v.active    = true;
    v.releasing = false;
    v.serial++;
    v.order      = order_++;
    v.start      = p.start;
    v.length     = p.length > 2.0f ? p.length : 2.0f;
    v.inv_length = 1.0f / v.length;
    v.offset     = 0.0f;
    v.rate       = p.rate > 0.01f ? p.rate : 0.01f;
    v.reverse    = p.reverse;
    v.passes     = p.passes;
    v.pass       = 0;
    v.gain       = p.gain;
    v.level      = 1.0f;
    v.pass_gain  = p.pass_gain;
    v.env        = p.env;
    v.soft        = p.soft;
    v.next_length = 0.0f;
    v.release    = 1.0f;
    v.tag        = p.tag;
    SetFade(v);

    // Equal-power balance of the stereo slice, unity at the centre.
    float pan = Clamp(p.pan, 0.0f, 1.0f);
    v.pan_l   = 1.41421356f * FastSin(0.25f * (1.0f - pan));
    v.pan_r   = 1.41421356f * FastSin(0.25f * pan);

    v.filter = p.filter;
    if(v.filter != VoiceFilter::kNone)
    {
        v.svf_l.Reset();
        v.svf_r.Reset();
        v.svf_l.Set(p.cutoff, p.q, sample_rate_);
        v.svf_r.CopyCoefs(v.svf_l);
    }

    v.crush_hold  = p.crush_hold > 1 ? p.crush_hold : 1;
    v.crush_count = 0;
    v.crush_q     = p.crush_bits > 0 ? (float)(1 << (p.crush_bits - 1)) : 0.0f;
    v.held_l = v.held_r = 0.0f;

    return ((Handle)v.serial << 8) | (Handle)(free_index + 1);
}

VoicePool::Voice *VoicePool::Find(Handle h)
{
    int index = (int)(h & 0xFF) - 1;
    if(index < 0 || index >= kMaxVoices)
        return nullptr;
    Voice &v = voices_[index];
    if(!v.active || v.serial != (uint16_t)(h >> 8))
        return nullptr;
    return &v;
}

bool VoicePool::Active(Handle h) const
{
    return const_cast<VoicePool *>(this)->Find(h) != nullptr;
}

void VoicePool::SetFade(Voice &v)
{
    // The fade is fixed in time, so as a fraction of a pass it depends on
    // the rate. Never more than half a pass.
    float frac = fmaxf(fade_frames_ * v.rate * v.inv_length, v.soft);
    v.inv_fade = frac < 0.5f ? 1.0f / frac : 2.0f;
}

void VoicePool::SetRate(Handle h, float rate)
{
    if(Voice *v = Find(h))
    {
        v->rate = rate > 0.01f ? rate : 0.01f;
        SetFade(*v);
    }
}

void VoicePool::SetLength(Handle h, float length)
{
    // Changing the length mid-pass would jump the envelope, so wait for the
    // end of the pass.
    if(Voice *v = Find(h))
        v->next_length = length > 2.0f ? length : 2.0f;
}

void VoicePool::SetPassGain(Handle h, float pass_gain)
{
    if(Voice *v = Find(h))
        v->pass_gain = pass_gain;
}

void VoicePool::SetGain(Handle h, float gain)
{
    if(Voice *v = Find(h))
        v->gain = gain;
}

void VoicePool::SetFilter(Handle h, float cutoff, float q)
{
    if(Voice *v = Find(h))
    {
        if(v->filter == VoiceFilter::kNone)
            return;
        v->svf_l.Set(cutoff, q, sample_rate_);
        v->svf_r.CopyCoefs(v->svf_l);
    }
}

void VoicePool::Release(Handle h)
{
    if(Voice *v = Find(h))
        v->releasing = true;
}

void VoicePool::ReleaseTag(uint8_t tag)
{
    for(auto &v : voices_)
        if(v.active && v.tag == tag)
            v.releasing = true;
}

void VoicePool::ReleaseAll()
{
    for(auto &v : voices_)
        if(v.active)
            v.releasing = true;
}

int VoicePool::NumActive() const
{
    int n = 0;
    for(const auto &v : voices_)
        n += v.active ? 1 : 0;
    return n;
}

void VoicePool::Process(const SampleBuffer &buf, float *out_l, float *out_r)
{
    float sum_l = 0.0f, sum_r = 0.0f;
    for(auto &v : voices_)
    {
        if(!v.active)
            continue;

        float p    = v.offset * v.inv_length;
        float fade = fminf(fminf(p, 1.0f - p) * v.inv_fade, 1.0f);
        float amp  = Shape(v.env, p) * fade * v.gain * v.level * v.release;

        float    pos  = v.reverse ? v.length - v.offset : v.offset;
        uint32_t ipos = (uint32_t)pos;
        float    l, r;
        buf.Read(v.start + ipos, pos - (float)ipos, &l, &r);

        if(v.filter != VoiceFilter::kNone)
        {
            float bp_l, bp_r;
            float lp_l = v.svf_l.Process(l, &bp_l);
            float lp_r = v.svf_r.Process(r, &bp_r);
            bool  lp   = v.filter == VoiceFilter::kLowPass;
            l          = lp ? lp_l : bp_l;
            r          = lp ? lp_r : bp_r;
        }
        if(v.crush_q > 0.0f || v.crush_hold > 1)
        {
            if(v.crush_count == 0)
            {
                v.held_l = l;
                v.held_r = r;
                if(v.crush_q > 0.0f)
                {
                    v.held_l = floorf(l * v.crush_q + 0.5f) / v.crush_q;
                    v.held_r = floorf(r * v.crush_q + 0.5f) / v.crush_q;
                }
            }
            if(++v.crush_count >= v.crush_hold)
                v.crush_count = 0;
            l = v.held_l;
            r = v.held_r;
        }

        sum_l += l * amp * v.pan_l;
        sum_r += r * amp * v.pan_r;

        if(v.releasing)
        {
            v.release -= release_inc_;
            if(v.release <= 0.0f)
            {
                v.active = false;
                continue;
            }
        }

        v.offset += v.rate;
        if(v.offset >= v.length)
        {
            v.offset -= v.length;
            v.pass++;
            v.level *= v.pass_gain;
            if(v.passes > 0 && v.pass >= v.passes)
                v.active = false;
            if(v.next_length > 0.0f)
            {
                v.length      = v.next_length;
                v.inv_length  = 1.0f / v.length;
                v.next_length = 0.0f;
                if(v.offset >= v.length)
                    v.offset = 0.0f;
                SetFade(v);
            }
        }
    }
    *out_l = sum_l;
    *out_r = sum_r;
}

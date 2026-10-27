#include "voice.h"

#include <cmath>

namespace
{
constexpr float kPhaseScale = 4294967296.0f; // a cycle of the 32-bit phase
constexpr int   kIndexShift = 25;            // 128 samples: the top 7 bits
} // namespace

void Voice::Init(const Wavetables *tables, float sample_rate, size_t, uint32_t seed)
{
    tables_      = tables;
    sample_rate_ = sample_rate;
    rng_.Seed(seed);
    phase_[0] = rng_.Next();
    phase_[1] = rng_.Next();
}

void Voice::Start(int note, float velocity, float from, bool retrigger)
{
    note_     = note;
    velocity_ = velocity;
    pitch_    = from;
    if(retrigger)
    {
        pan_rand_ = rng_.Bi();
        for(Adsr &e : env_)
            e.Gate(true);
    }
}

void Voice::Release()
{
    for(Adsr &e : env_)
        e.Gate(false);
}

void Voice::Kill()
{
    for(Adsr &e : env_)
        e.Reset();
    gain_ = 0.0f;
    filter_.Reset();
}

void Voice::Render(const Frame &f, float *__restrict l, float *__restrict r, size_t size)
{
    float env[kNumEnvs];
    for(int e = 0; e < kNumEnvs; e++)
        env[e] = env_[e].Process(f.env[e]);

    pitch_ += ((float)note_ - pitch_) * f.glide;

    uint32_t      inc[2];
    const int8_t *wave[2];
    for(int o = 0; o < 2; o++)
    {
        const Frame::Osc &osc = f.osc[o];
        float note = (osc.fixed ? 60.0f : pitch_) + osc.offset + f.bend;
        float hz   = 440.0f * dsp::FastExp2((note - 69.0f) * (1.0f / 12.0f));
        inc[o]     = (uint32_t)(dsp::Clamp(hz / sample_rate_, 0.0f, 0.49f) * kPhaseScale);

        float pos = osc.wave + env[kWaveEnv] * (osc.env + osc.velocity * velocity_);
        wave_[o]  = (int)dsp::Clamp(pos + 0.5f, 0.0f, (float)osc.top);
        wave[o]   = osc.table + wave_[o] * Wavetables::kLength;
    }

    cutoff_ = f.cutoff + env[kFilterEnv] * (f.filter_env + f.filter_vel * velocity_)
              + f.track * ((float)note_ - 60.0f) * (1.0f / 12.0f);
    filter_.Set(f.filter, cutoff_, f.resonance, sample_rate_);

    // The amplifier ramps to its new level across the block.
    float level = env[kAmpEnv] * f.volume * (1.0f - f.amp_velocity * (1.0f - velocity_));
    float step  = (level - gain_) / (float)size;
    float gain  = gain_;
    gain_       = level;

    float pan    = dsp::Clamp(f.pan + pan_rand_ * f.pan_spread, -1.0f, 1.0f);
    float pan_l  = dsp::FastSin(0.25f + 0.125f * (pan + 1.0f)); // cos, equal power
    float pan_r  = dsp::FastSin(0.125f * (pan + 1.0f));
    float level1 = f.osc[0].level * (1.0f / 127.0f);
    float level2 = f.osc[1].level * (1.0f / 127.0f);

    for(size_t i = 0; i < size; i++)
    {
        float s = level1 * (float)wave[0][phase_[0] >> kIndexShift]
                  + level2 * (float)wave[1][phase_[1] >> kIndexShift];
        phase_[0] += inc[0];
        phase_[1] += inc[1];
        if(f.dirt > 0.0f)
            s += f.dirt * rng_.Bi();
        gain += step;
        float y = filter_.Process(s) * gain;
        l[i] += y * pan_l;
        r[i] += y * pan_r;
    }
}

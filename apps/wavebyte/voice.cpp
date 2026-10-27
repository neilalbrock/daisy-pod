#include "voice.h"

#include <cmath>

namespace
{
constexpr float kPhaseScale = 4294967296.0f; // a cycle of the 32-bit phase
constexpr int   kIndexShift = 25;            // 128 samples: the top 7 bits
constexpr float kFracScale  = 1.0f / 33554432.0f; // the bits below them, as 0..1

// Flavour at full: how far each thing moves, drift and the note's offset
// together.
constexpr float kFlavourWaves  = 3.0f;  // wave positions
constexpr float kFlavourOct    = 0.3f;  // cutoff
constexpr float kFlavourLevel  = 0.12f; // level, per note
constexpr float kDacGainError  = 0.06f; // at full DAC
constexpr float kDacZeroError  = 0.5f;  // the dead band around zero, in converter steps
} // namespace

void Voice::Init(const Wavetables *tables, float sample_rate, size_t block_size, uint32_t seed)
{
    tables_      = tables;
    sample_rate_ = sample_rate;
    rng_.Seed(seed);
    phase_[0] = rng_.Next();
    phase_[1] = rng_.Next();
    dirt_.Init(sample_rate, rng_.Next());

    // The voice's own drift rates (a slow one, a middling one, a quick
    // shimmer), and its DAC's tolerances.
    const float lo[kDrifts] = {0.11f, 0.6f, 3.0f}, hi[kDrifts] = {0.35f, 1.7f, 5.5f};
    float       block_rate  = sample_rate / (float)block_size;
    for(int d = 0; d < kDrifts; d++)
    {
        drift_rate_[d]  = rng_.Range(lo[d], hi[d]) / block_rate;
        drift_phase_[d] = rng_.Uni();
    }
    dac_gain_ = rng_.Bi();
    dac_zero_ = rng_.Bi();
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
        note_wave_[0] = rng_.Bi();
        note_wave_[1] = rng_.Bi();
        note_cutoff_  = rng_.Bi();
        note_pitch_   = rng_.Bi();
        note_level_   = rng_.Bi();
        restart_dirt_ = true;
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

// This block's Flavour: osc 1's pitch (semitones), the wave positions,
// cutoff (octaves) and level (a factor).
void Voice::Flavour(const Frame &f, float *pitch1, float wave[2], float *cutoff, float *level)
{
    float s[kDrifts];
    for(int d = 0; d < kDrifts; d++)
    {
        drift_phase_[d] += drift_rate_[d];
        if(drift_phase_[d] >= 1.0f)
            drift_phase_[d] -= 1.0f;
        s[d] = dsp::FastSin(drift_phase_[d]);
    }
    float amount = f.flavour;

    // Pitch: mostly the slow drift, a little shimmer, and the note's own.
    drift_ = amount * f.flavour_pitch * (0.5f * s[0] + 0.15f * s[2] + 0.35f * note_pitch_);
    *pitch1 += drift_;

    // Each oscillator's wave on its own path: the second runs the drifts
    // the other way round.
    wave[0] += amount * kFlavourWaves * (0.6f * s[1] + 0.4f * note_wave_[0]);
    wave[1] += amount * kFlavourWaves * (0.6f * s[0] + 0.4f * note_wave_[1]);
    *cutoff += amount * kFlavourOct * (0.5f * s[1] + 0.5f * note_cutoff_);
    *level *= 1.0f + amount * kFlavourLevel * note_level_;
}

void Voice::Render(const Frame &f, float *__restrict l, float *__restrict r, size_t size)
{
    if(restart_dirt_)
    {
        dirt_.Trigger(f.dirt_type);
        restart_dirt_ = false;
    }

    float env[kNumEnvs];
    for(int e = 0; e < kNumEnvs; e++)
        env[e] = env_[e].Process(f.env[e]);

    pitch_ += ((float)note_ - pitch_) * f.glide;

    float pitch1 = 0.0f, flavour_cutoff = 0.0f, flavour_level = 1.0f;
    float wave_pos[2];
    for(int o = 0; o < 2; o++)
    {
        const Frame::Osc &osc = f.osc[o];
        wave_pos[o]           = osc.wave + env[kWaveEnv] * (osc.env + osc.velocity * velocity_);
    }
    drift_ = 0.0f;
    if(f.flavour > 0.0f)
        Flavour(f, &pitch1, wave_pos, &flavour_cutoff, &flavour_level);

    uint32_t inc[2];
    for(int o = 0; o < 2; o++)
    {
        const Frame::Osc &osc = f.osc[o];
        float note = (osc.fixed ? 60.0f : pitch_) + osc.offset + f.bend + (o == 0 ? pitch1 : 0.0f);
        float hz   = 440.0f * dsp::FastExp2((note - 69.0f) * (1.0f / 12.0f));
        inc[o]     = (uint32_t)(dsp::Clamp(hz / sample_rate_, 0.0f, 0.49f) * kPhaseScale);

        float top = (float)osc.top;
        if(f.clean)
        {
            // Between two waves, crossfaded.
            float pos    = dsp::Clamp(wave_pos[o], 0.0f, top);
            int   a      = (int)pos;
            int   b      = a < osc.top ? a + 1 : a;
            wave_[o]     = pos;
            wave_mix_[o] = pos - (float)a;
            wave_a_[o]   = osc.table + a * Wavetables::kLength;
            wave_b_[o]   = osc.table + b * Wavetables::kLength;
        }
        else
        {
            int a      = (int)dsp::Clamp(wave_pos[o] + 0.5f, 0.0f, top);
            wave_[o]   = (float)a;
            wave_a_[o] = osc.table + a * Wavetables::kLength;
        }
    }

    cutoff_ = f.cutoff + env[kFilterEnv] * (f.filter_env + f.filter_vel * velocity_)
              + f.track * ((float)note_ - 60.0f) * (1.0f / 12.0f) + flavour_cutoff;
    filter_.Set(f.filter, cutoff_, f.resonance, sample_rate_);

    // The amplifier ramps to its new level across the block.
    float level = env[kAmpEnv] * f.volume * (1.0f - f.amp_velocity * (1.0f - velocity_)) * flavour_level;
    float step  = (level - gain_) / (float)size;
    float gain  = gain_;
    gain_       = level;

    float pan   = dsp::Clamp(f.pan + pan_rand_ * f.pan_spread, -1.0f, 1.0f);
    float pan_l = dsp::FastSin(0.25f + 0.125f * (pan + 1.0f)); // cos, equal power
    float pan_r = dsp::FastSin(0.125f * (pan + 1.0f));

    if(f.clean)
        Run<true>(f, inc, l, r, size, gain, step, pan_l, pan_r);
    else
        Run<false>(f, inc, l, r, size, gain, step, pan_l, pan_r);
}

template <bool kClean>
void Voice::Run(const Frame &f, const uint32_t inc[2], float *__restrict l, float *__restrict r,
                size_t size, float gain, float step, float pan_l, float pan_r)
{
    const float level1 = f.osc[0].level * (1.0f / 127.0f);
    const float level2 = f.osc[1].level * (1.0f / 127.0f);
    const float dirt   = f.dirt;
    const Dirt::Type dirt_type = f.dirt_type;

    // The DAC: a step of dac_step, the voice's gain error, and its dead band
    // around zero; off at 0.
    const bool  dac      = f.dac_step > 0.0f;
    const float dac_step = f.dac_step * full_scale_;
    const float dac_inv  = dac ? 1.0f / dac_step : 0.0f;
    const float dac_gain = 1.0f + kDacGainError * f.dac * dac_gain_;
    const float dac_zero = kDacZeroError * (0.5f + 0.5f * dac_zero_) * dac_step;

    const int8_t *a1 = wave_a_[0], *a2 = wave_a_[1];
    const int8_t *b1 = wave_b_[0], *b2 = wave_b_[1];
    const float   mix1 = wave_mix_[0], mix2 = wave_mix_[1];

    for(size_t i = 0; i < size; i++)
    {
        float o1, o2; // -127..127
        if(kClean)
        {
            uint32_t i1 = phase_[0] >> kIndexShift, i2 = phase_[1] >> kIndexShift;
            uint32_t n1 = (i1 + 1) & (Wavetables::kLength - 1), n2 = (i2 + 1) & (Wavetables::kLength - 1);
            float    t1 = (float)(phase_[0] & 0x1FFFFFFu) * kFracScale;
            float    t2 = (float)(phase_[1] & 0x1FFFFFFu) * kFracScale;
            float    x1 = dsp::Lerp((float)a1[i1], (float)a1[n1], t1);
            float    y1 = dsp::Lerp((float)b1[i1], (float)b1[n1], t1);
            float    x2 = dsp::Lerp((float)a2[i2], (float)a2[n2], t2);
            float    y2 = dsp::Lerp((float)b2[i2], (float)b2[n2], t2);
            o1          = dsp::Lerp(x1, y1, mix1);
            o2          = dsp::Lerp(x2, y2, mix2);
        }
        else
        {
            o1 = (float)a1[phase_[0] >> kIndexShift];
            o2 = (float)a2[phase_[1] >> kIndexShift];
        }
        phase_[0] += inc[0];
        phase_[1] += inc[1];

        float s = level1 * o1 + level2 * o2;
        if(dirt > 0.0f)
            s += dirt * dirt_.Process(dirt_type, o1 * o2 * (1.0f / 16129.0f));

        gain += step;
        float y = filter_.Process(s) * gain;
        if(dac)
        {
            y = y * dac_gain;
            y -= dsp::Clamp(y, -dac_zero, dac_zero);
            y = roundf(y * dac_inv) * dac_step;
        }
        l[i] += y * pan_l;
        r[i] += y * pan_r;
    }
}

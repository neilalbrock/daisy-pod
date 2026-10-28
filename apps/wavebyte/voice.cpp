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
    tables_          = tables;
    sample_rate_     = sample_rate;
    inv_sample_rate_ = 1.0f / sample_rate;
    rng_.Seed(seed);
    phase_[0] = rng_.Next();
    phase_[1] = rng_.Next();
    dirt_.Init(sample_rate, rng_.Next());

    // The voice's own drift rates (a slow one, a middling one, a quick
    // shimmer), and its DAC's tolerances.
    const float lo[kDrifts] = {0.11f, 0.6f, 3.0f}, hi[kDrifts] = {0.35f, 1.7f, 5.5f};
    block_rate_ = sample_rate / (float)block_size;
    for(int d = 0; d < kDrifts; d++)
    {
        drift_rate_[d]  = rng_.Range(lo[d], hi[d]) / block_rate_;
        drift_phase_[d] = rng_.Uni();
    }
    dac_gain_ = rng_.Bi();
    dac_zero_ = rng_.Bi();
    lfo_[0].Seed(rng_.Next());
    lfo_[1].Seed(rng_.Next());
}

void Voice::Start(int note, float velocity, float from, bool retrigger, int channel)
{
    note_     = note;
    velocity_ = velocity;
    pitch_    = from;
    channel_  = channel & 15;
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
        random_       = rng_.Uni();
        random_bi_    = rng_.Bi();
        restart_dirt_ = true;
        lfo_[0].Restart(); // Free Voice: every note starts its own
        lfo_[1].Restart();
    }
}

void Voice::Release(float velocity)
{
    release_velocity_ = velocity;
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

// A source's value for this voice, 0..1 or -1..1.
float Voice::Source(mod::Source s, const Frame &f) const
{
    const mod::Controllers &c = *f.controllers;
    float poly  = c.mpe ? c.channel_pressure[channel_] : c.poly_pressure[note_ & 127];
    float touch = fmaxf(c.pressure, poly);
    switch(s)
    {
        case mod::kWaveEnv: return env_[kWaveEnv].Level();
        case mod::kFilterEnv: return env_[kFilterEnv].Level();
        case mod::kAmpEnv: return env_[kAmpEnv].Level();
        case mod::kLfo1: return lfo_out_[0];
        case mod::kLfo1Wheel: return lfo_out_[0] * c.wheel;
        case mod::kLfo1Touch: return lfo_out_[0] * touch;
        case mod::kLfo2: return lfo_out_[1];
        case mod::kLfo2Wheel: return lfo_out_[1] * c.wheel;
        case mod::kLfo2Touch: return lfo_out_[1] * touch;
        case mod::kModWheel: return c.wheel;
        case mod::kPitchBend: return c.bend;
        case mod::kNotePitch: return c.mpe ? c.channel_bend[channel_] : 0.0f;
        case mod::kChanPressure: return c.pressure;
        case mod::kPolyPressure: return poly;
        case mod::kMpeY: return c.mpe ? c.channel_y[channel_] : 0.0f;
        case mod::kPedal: return c.pedal;
        case mod::kExpression: return c.expression;
        case mod::kBreath: return c.breath;
        case mod::kKeytrack: return ((float)note_ - 60.0f) * (1.0f / 60.0f);
        case mod::kVelocity: return velocity_;
        case mod::kReleaseVelocity: return release_velocity_;
        case mod::kRandom: return random_;
        case mod::kRandomBi: return random_bi_;
        case mod::kConstant: return 1.0f;
        default:
            if(s >= mod::kCc22 && s <= mod::kCc31)
                return c.cc[s - mod::kCc22];
            return 0.0f;
    }
}

// The LFOs, then the matrix: what each target moves by this block.
void Voice::Modulate(const Frame &f)
{
    for(int i = 0; i < 2; i++)
    {
        if(f.lfo_mode[i] == Lfo::kFreeVoice)
        {
            // Its speed follows the matrix, a block behind.
            float rate  = f.lfo_rate[i] * dsp::FastExp2(mod_[mod::kLfo1Speed + i]);
            lfo_out_[i] = lfo_[i].Advance(rate, f.lfo_shape[i]);
        }
        else
            lfo_out_[i] = f.controllers->lfo[i];
    }

    for(float &m : mod_)
        m = 0.0f;
    for(int i = 0; i < f.num_slots; i++)
    {
        const mod::Slot &slot = f.slots[i];
        mod_[slot.target] += slot.amount * Source(slot.source, f);
    }
}

void Voice::Render(const Frame &f, float *__restrict l, float *__restrict r, size_t size)
{
    if(restart_dirt_)
    {
        dirt_.Trigger(f.dirt_type);
        restart_dirt_ = false;
    }

    Modulate(f);

    // The envelopes, with their own rates where the matrix moves them.
    float env[kNumEnvs];
    for(int e = 0; e < kNumEnvs; e++)
    {
        if(f.env_modulated[e])
        {
            const float *t = f.env_times[e];
            const float *m = mod_ + mod::kEnvStages + 4 * e;
            Adsr::Coefs  c = f.env[e];
            // Only the stages a slot moves this block.
            if(m[0] != 0.0f)
                c.attack_step = 1.0f / (t[0] * dsp::FastExp2(-m[0]) * block_rate_);
            if(m[1] != 0.0f)
                c.decay = Adsr::Coefs::Fall(t[1] * dsp::FastExp2(-m[1]), block_rate_);
            if(m[2] != 0.0f)
                c.sustain = dsp::Clamp(t[2] + m[2], 0.0f, 1.0f);
            if(m[3] != 0.0f)
                c.release = Adsr::Coefs::Fall(t[3] * dsp::FastExp2(-m[3]), block_rate_);
            env[e] = env_[e].Process(c);
        }
        else
            env[e] = env_[e].Process(f.env[e]);
    }

    pitch_ += ((float)note_ - pitch_) * f.glide;

    float pitch1 = 0.0f, flavour_cutoff = 0.0f, flavour_level = 1.0f;
    float wave_pos[2];
    for(int o = 0; o < 2; o++)
    {
        const Frame::Osc &osc = f.osc[o];
        wave_pos[o] = osc.wave + env[kWaveEnv] * (osc.env + osc.velocity * velocity_) + mod_[mod::kWave1 + o];
    }
    drift_ = 0.0f;
    if(f.flavour > 0.0f)
        Flavour(f, &pitch1, wave_pos, &flavour_cutoff, &flavour_level);

    // With MPE, the note's own bend, over 48 semitones.
    const mod::Controllers &c = *f.controllers;
    float note_bend = c.mpe ? c.channel_bend[channel_] * f.note_bend_range : 0.0f;

    uint32_t inc[2];
    Mix      mix;
    for(int o = 0; o < 2; o++)
    {
        const Frame::Osc &osc = f.osc[o];
        float note = (osc.fixed ? 60.0f : pitch_ + note_bend) + osc.offset + f.bend
                     + (o == 0 ? pitch1 : 0.0f) + mod_[mod::kPitch] + mod_[mod::kPitch1 + o];
        mix.level[o] = dsp::Clamp(osc.level + mod_[mod::kLevel1 + o], 0.0f, 1.0f) * (1.0f / 127.0f);
        float hz   = 440.0f * dsp::FastExp2((note - 69.0f) * (1.0f / 12.0f));
        inc[o]     = (uint32_t)(dsp::Clamp(hz * inv_sample_rate_, 0.0f, 0.49f) * kPhaseScale);

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

    float dirt = f.dirt;
    if(mod_[mod::kDirt] != 0.0f)
    {
        float knob = dsp::Clamp(f.dirt_knob + mod_[mod::kDirt], 0.0f, 1.0f);
        dirt       = knob * knob;
    }
    mix.dirt = dirt;

    cutoff_ = f.cutoff + env[kFilterEnv] * (f.filter_env + f.filter_vel * velocity_)
              + f.track * ((float)note_ - 60.0f) * (1.0f / 12.0f) + flavour_cutoff + mod_[mod::kCutoff];
    filter_.Set(f.filter, cutoff_, f.resonance + mod_[mod::kResonance], sample_rate_);

    // The amplifier ramps to its new level across the block.
    float level = env[kAmpEnv] * f.volume * (1.0f - f.amp_velocity * (1.0f - velocity_)) * flavour_level
                  * dsp::Clamp(1.0f + mod_[mod::kVolume], 0.0f, 2.0f);
    float step  = (level - gain_) / (float)size;
    float gain  = gain_;
    gain_       = level;

    float pan   = dsp::Clamp(f.pan + pan_rand_ * f.pan_spread + mod_[mod::kPan], -1.0f, 1.0f);
    float pan_l = dsp::FastSin(0.25f + 0.125f * (pan + 1.0f)); // cos, equal power
    float pan_r = dsp::FastSin(0.125f * (pan + 1.0f));

    if(f.clean)
        Run<true>(f, mix, inc, l, r, size, gain, step, pan_l, pan_r);
    else
        Run<false>(f, mix, inc, l, r, size, gain, step, pan_l, pan_r);
}

template <bool kClean>
void Voice::Run(const Frame &f, const Mix &mix, const uint32_t inc[2], float *__restrict l,
                float *__restrict r, size_t size, float gain, float step, float pan_l, float pan_r)
{
    const float level1 = mix.level[0];
    const float level2 = mix.level[1];
    const float dirt   = mix.dirt;
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

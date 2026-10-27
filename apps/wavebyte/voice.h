// Wavebyte - one voice
//
// Two wavetable oscillators, Dirt (dirt.h), the filter and the amplifier,
// with the three envelopes (wave, filter, amp). In 8-bit mode the
// oscillators read their 128-sample waves with no interpolation, as the
// Microwave's chip did, and step from wave to wave rather than
// crossfading: that and the 8-bit samples are the grit. Clean mode
// interpolates between samples and crossfades between waves. Wave
// position, cutoff and level are worked out once per block from the
// envelopes, velocity, keytrack, glide and Flavour; the samples in between
// follow.
//
// Flavour is small, steady variation, mostly in wave position and pitch: a
// few slow drifts at rates of the voice's own (each voice has its own
// character, so the variations are methodical rather than random) and a
// small offset drawn per note. Its pitch drift is on osc 1 only and scales
// with Bend Range, as on the Protein: a range of 0 means none.
//
// The amplifier models the voice's DAC, as the Protein's does: the level
// is applied digitally and the result converted at a few bits (12 down to
// 6 with the DAC setting), so a quiet tail turns grainy, and each voice's
// converter has its own small gain error and a dead band around zero
// (crossover distortion, where every bit flips), which roughens quiet
// sounds most.
//
// The engine fills a Frame (what all voices share this block) and calls
// Render on each sounding voice. libDaisy-free.

#pragma once

#include <cstddef>
#include <cstdint>

#include "adsr.h"
#include "dirt.h"
#include "dsp.h"
#include "voice_filter.h"
#include "wavetables.h"

class Voice
{
  public:
    enum Env
    {
        kWaveEnv,
        kFilterEnv,
        kAmpEnv,
        kNumEnvs,
    };

    // This block's settings, shared by every voice.
    struct Frame
    {
        struct Osc
        {
            const int8_t *table;    // the wavetable's first wave
            float         wave;     // 0..63
            float         env;      // waves the envelope moves it at full level
            float         velocity; // ... and more, at full velocity
            int           top;      // the last wave it may reach
            float         level;
            float         offset; // semitones: Semitone and Detune
            bool          fixed;  // ignores the note
        } osc[2];

        float bend;  // semitones
        float glide; // one-pole coefficient per block; 1: none

        VoiceFilter::Type filter;
        float             cutoff;     // octaves above 20 Hz
        float             resonance;  // 0..1
        float             filter_env; // octaves at full envelope
        float             filter_vel; // ... and more, at full velocity
        float             track;      // octaves per octave from note 60

        float      dirt;      // Dirt level
        Dirt::Type dirt_type;

        float flavour;       // 0..1
        float flavour_pitch; // osc 1's drift at full Flavour, semitones (from Bend Range)
        bool  clean;         // interpolate and crossfade, else 8-bit
        float dac;           // 0: an ideal converter, 1: 6 bits and worn
        float dac_step;      // the converter's step at that setting (0: off)

        Adsr::Coefs env[kNumEnvs];

        float volume;
        float amp_velocity; // 0: velocity doesn't matter, 1: it sets the level
        float pan;          // -1..1
        float pan_spread;   // random, per note
    };

    void Init(const Wavetables *tables, float sample_rate, size_t block_size, uint32_t seed);

    // The DAC's full scale: the most a voice puts out.
    static void SetFullScale(float level) { full_scale_ = level; }

    // A note starts, or takes over this voice: glides from `from` (a note
    // number), and with retrigger, the envelopes start again.
    void Start(int note, float velocity, float from, bool retrigger);
    void Release();
    void Kill(); // silent at once

    // Adds this block's samples into l and r (which nothing else points
    // into, so the filter's state can stay in registers).
    void Render(const Frame &f, float *__restrict l, float *__restrict r, size_t size);

    bool     Active() const { return !env_[kAmpEnv].Idle(); }
    bool     Released() const { return env_[kAmpEnv].Released(); }
    int      Note() const { return note_; }
    float    Pitch() const { return pitch_; }
    uint32_t Age() const { return started_; }
    void     SetAge(uint32_t n) { started_ = n; }

    // For the web page.
    float WaveAt(int osc) const { return (float)wave_[osc]; }
    float CutoffHz() const { return VoiceFilter::CutoffHz(cutoff_); }
    float AmpEnv() const { return env_[kAmpEnv].Level(); }
    float DriftCents() const { return 100.0f * drift_; } // Flavour's on osc 1

  private:
    static constexpr int kDrifts = 3;

    template <bool kClean>
    void Run(const Frame &f, const uint32_t inc[2], float *__restrict l, float *__restrict r,
             size_t size, float gain, float step, float pan_l, float pan_r);

    void Flavour(const Frame &f, float *pitch1, float wave[2], float *cutoff, float *level);

    static inline float full_scale_ = 0.35f;

    const Wavetables *tables_ = nullptr;
    float             sample_rate_ = 48000.0f;
    dsp::Rng          rng_;

    int      note_     = 60;
    float    velocity_ = 1.0f;
    float    pitch_    = 60.0f; // gliding towards note_
    uint32_t started_  = 0;
    float    pan_rand_ = 0.0f; // -1..1, drawn per note

    uint32_t phase_[2] = {};
    float    wave_[2]  = {}; // positions this block
    const int8_t *wave_a_[2] = {}, *wave_b_[2] = {}; // the waves read (b: crossfaded to, in Clean)
    float    wave_mix_[2] = {};
    float    cutoff_   = 10.0f;
    float    gain_     = 0.0f; // where the last block's amplitude ended

    // Flavour: drifts at the voice's own rates, and the note's offsets.
    float drift_phase_[kDrifts] = {}, drift_rate_[kDrifts] = {}; // cycles, per block
    float note_wave_[2] = {}, note_cutoff_ = 0.0f, note_pitch_ = 0.0f, note_level_ = 0.0f;
    float drift_ = 0.0f; // osc 1's this block, semitones
    bool  restart_dirt_ = false; // a note started: Click and the Bursts fire

    // The DAC's tolerances, the voice's own: gain error and dead band (-1..1).
    float dac_gain_ = 0.0f, dac_zero_ = 0.0f;

    Adsr        env_[kNumEnvs];
    VoiceFilter filter_;
    Dirt        dirt_;
};

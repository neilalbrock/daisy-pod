// Wavebyte - one voice
//
// Two wavetable oscillators, the Dirt noise, the filter and the amplifier,
// with the three envelopes (wave, filter, amp). The oscillators read their
// 128-sample waves with no interpolation, as the Microwave's chip did, and
// step from wave to wave rather than crossfading: that and the 8-bit
// samples are the grit. Wave position, cutoff and level are worked out once
// per block from the envelopes, velocity, keytrack and glide; the samples
// in between follow.
//
// The engine fills a Frame (what all voices share this block) and calls
// Render on each sounding voice. libDaisy-free.

#pragma once

#include <cstddef>
#include <cstdint>

#include "adsr.h"
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

        float dirt; // noise level

        Adsr::Coefs env[kNumEnvs];

        float volume;
        float amp_velocity; // 0: velocity doesn't matter, 1: it sets the level
        float pan;          // -1..1
        float pan_spread;   // random, per note
    };

    void Init(const Wavetables *tables, float sample_rate, size_t block_size, uint32_t seed);

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

  private:
    const Wavetables *tables_ = nullptr;
    float             sample_rate_ = 48000.0f;
    dsp::Rng          rng_;

    int      note_     = 60;
    float    velocity_ = 1.0f;
    float    pitch_    = 60.0f; // gliding towards note_
    uint32_t started_  = 0;
    float    pan_rand_ = 0.0f; // -1..1, drawn per note

    uint32_t phase_[2] = {};
    int      wave_[2]  = {};
    float    cutoff_   = 10.0f;
    float    gain_     = 0.0f; // where the last block's amplitude ended

    Adsr        env_[kNumEnvs];
    VoiceFilter filter_;
};

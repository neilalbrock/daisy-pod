// Wavebyte - the synth engine
//
// Eight voices, played as Poly (a voice per note: a free one, else the
// oldest released, else the oldest), Mono (one voice, retriggered by each
// note; let go of the note playing and the highest still held takes over,
// as on the Protein) or Legato (as Mono, but only a note played from
// silence restarts the envelopes). Glide moves each new note from the last
// one played: always (Onset) or only while another key is held (Legato).
// The sustain pedal holds notes released while it's down.
//
// The patch is the Pod's and the web page's values, as they set them:
// knobs 0..1 and choices as indices. The engine maps them to units, so the
// mappings live in one place (web/js/apps/wavebyte.js shows the same).
//
// Call the note functions and SetPatch from the audio callback, then
// Process once per block. libDaisy-free.

#pragma once

#include <cstddef>
#include <cstdint>

#include "voice.h"
#include "voice_filter.h"
#include "wavetables.h"

class SynthEngine
{
  public:
    static constexpr int kVoices = 8;

    enum VoiceMode
    {
        kPoly,
        kMono,
        kLegato,
        kNumVoiceModes,
    };

    enum GlideMode
    {
        kGlideOnset,
        kGlideLegato,
        kNumGlideModes,
    };

    static constexpr int kNumPanModes = 6; // Fix, then Rand 1-5

    enum OscMode
    {
        kOsc8Bit,
        kOscClean,
        kNumOscModes,
    };

    struct Patch
    {
        struct Osc
        {
            int   table    = 0;
            float wave     = 0.0f; // 0..1 across the 64 waves
            float env      = 0.5f; // bipolar, 0.5 = none
            float velocity = 0.5f; // bipolar
            float level    = 0.8f;
            float semitone = 0.5f; // -24..+24
            float detune   = 0.5f; // -1..+1 semitone
            bool  limit    = false; // keep off the last three waves
            bool  fixed    = false;
        } osc[2];

        int   voice_mode = kPoly;
        float glide      = 0.0f;
        int   glide_mode = kGlideOnset;
        float bend_range = 2.0f / 12.0f; // 0..12 semitones

        int   filter     = VoiceFilter::kLp24;
        float cutoff     = 0.7f;
        float resonance  = 0.2f;
        float filter_env = 0.5f; // bipolar
        float filter_vel = 0.5f; // bipolar
        float track      = 0.625f; // bipolar, -200%..+200%
        float dirt       = 0.0f;
        int   dirt_type  = Dirt::kStatic;

        float flavour  = 0.0f;
        float dac      = 0.3f;
        int   osc_mode = kOsc8Bit;

        // Wave, filter and amp envelopes: attack, decay, sustain, release.
        float env[Voice::kNumEnvs][4] = {
            {0.0f, 0.5f, 0.0f, 0.5f},
            {0.0f, 0.5f, 0.3f, 0.5f},
            {0.0f, 0.5f, 1.0f, 0.4f},
        };

        float volume       = 0.8f;
        float amp_velocity = 0.5f;
        float pan          = 0.5f; // bipolar
        int   pan_mode     = 0;    // Fix, Rand 1-5
    };

    // What the web page shows: the voice last started.
    struct Probe
    {
        float wave[2]; // 0..63
        float cutoff;  // Hz
        float amp;     // its amp envelope
        int   note;
        float drift;  // Flavour's on osc 1, cents
        int   voices; // sounding
    };

    void Init(const Wavetables *tables, float sample_rate, size_t block_size);

    void SetPatch(const Patch &p);

    void NoteOn(int note, int velocity);
    void NoteOff(int note);
    void PitchBend(float amount) { bend_ = amount; } // -1..1
    void Sustain(bool down);
    void AllNotesOff();

    // Writes (not adds) one block.
    void Process(float *l, float *r, size_t size);

    Probe GetProbe() const;

    // The patch's values in units, for the engine and the page alike.
    static int   Semitones(float x) { return (int)(x * 48.0f + 0.5f) - 24; }
    static float Detune(float x) { return 2.0f * x - 1.0f; } // semitones
    static int   BendRange(float x) { return (int)(x * 12.0f + 0.5f); }
    static float GlideSeconds(float x); // 0: off
    static float Bipolar(float x) { return 2.0f * x - 1.0f; }
    static float DacBits(float x) { return 12.0f - 6.0f * x; } // for x > 0; 0 is off

  private:
    static constexpr int kMaxHeld = 16;

    int  Allocate();
    void Play(Voice &v, int note, float velocity, bool retrigger, bool held_before);
    void MonoTo(int note, float velocity, bool retrigger, bool held_before);
    bool Held(int note) const;

    const Wavetables *tables_ = nullptr;
    float             sample_rate_ = 48000.0f, block_rate_ = 12000.0f;
    Voice             voices_[kVoices];
    Voice::Frame      frame_ = {};
    Patch             patch_;
    float             glide_set_ = -1.0f; // the settings frame_'s coefficients are for
    float             dac_set_   = -1.0f;
    float             env_set_[Voice::kNumEnvs][4] = {{-1.0f}, {-1.0f}, {-1.0f}};
    uint32_t          started_ = 0; // notes so far, to age the voices
    int               last_voice_ = 0;
    float             last_note_ = 60.0f; // where the next glide starts
    float             bend_ = 0.0f;
    bool              sustain_ = false;

    // Keys down, oldest first.
    int   held_[kMaxHeld];
    int   num_held_ = 0;
    float velocity_ = 1.0f; // the last note's
    bool  sustained_[128] = {}; // released while the pedal was down
};

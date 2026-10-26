// Tempo from tap tempo or MIDI clock
//
// One beat position for an app's effects. As on the Microcosm (grainfall's
// model), MIDI Start (or Continue) hands timing to the incoming clock
// and MIDI Stop hands it back. If the clock just stops arriving, the
// internal clock carries on from where it was at the same tempo. Taps are
// ignored while following clock. Call it only from the audio callback, with
// a running sample count as `now`.

#pragma once

#include <cstddef>
#include <cstdint>

#include "midi_clock.h"

class Tempo
{
  public:
    // The beat position wraps here: four bars, a whole number of steps of
    // every subdivision.
    static constexpr int kWrapBeats = MidiClock::kWrapTicks / MidiClock::kTicksPerBeat;

    void Init(float sample_rate);

    void MidiTick(uint32_t now) { clock_.Tick(now); }
    void MidiStart();
    void MidiContinue() { external_ = true; }
    void MidiStop() { external_ = false; }

    // Quarter-note taps. The beat also lands on each tap.
    void Tap(uint32_t now);
    void SetBpm(float bpm);

    // Once per block, before reading the position.
    void Update(uint32_t now);

    float BeatPos() const { return beats_; } // 0..kWrapBeats
    float SamplesPerBeat() const { return samples_per_beat_; }
    bool  Following() const { return following_; } // locked to MIDI clock
    // A tempo was tapped in or set (not just the 120 BPM it starts at),
    // until ClearTempo(). An app can sync to it only then.
    bool HasTempo() const { return has_tempo_; }
    void ClearTempo() { has_tempo_ = false; }

  private:
    static constexpr int kTapHistory = 3;

    MidiClock clock_;
    float     sample_rate_      = 48000.0f;
    float     samples_per_beat_ = 24000.0f; // 120 BPM
    float     beats_            = 0.0f;
    uint32_t  last_now_         = 0;
    bool      external_         = false;
    bool      following_        = false;
    bool      has_tempo_        = false;

    uint32_t last_tap_  = 0;
    bool     have_tap_  = false;
    uint32_t taps_[kTapHistory] = {};
    int      num_taps_  = 0;
    int      next_tap_  = 0;
};

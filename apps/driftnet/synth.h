// Driftnet - Synth Mode
//
// MOOD MKII's Synth Mode: MIDI notes play the CLOCK in semitones, so
// whatever is in the loop or the Wet Channel becomes an instrument. Middle C
// (with OCTAVE at its default of 3) is 48k, where everything plays at the
// pitch it was recorded at; the clock's range, 2k to 64k, sets how far a
// note can go. A note turns it on; CC 59, or moving the CLOCK knob, turns it
// off.
//
// Notes are monophonic, last note first, gliding over PORTAMENTO. Pitch bend
// covers 4 semitones either way and the mod wheel adds vibrato. OUTPUT TYPE
// decides what's heard between notes: OPEN drones on regardless, ON/OFF
// sounds only while a key is down, and ADSR shapes each note with an
// envelope. Both follow velocity.
//
// Runs once per block, from MIDI handled in the same callback. libDaisy-free.

#pragma once

#include <cstdint>

class Synth
{
  public:
    enum class Output
    {
        kOpen,
        kOnOff,
        kAdsr,
    };

    struct Settings
    {
        Output output     = Output::kOpen;
        int    octave     = 3;    // OCTAVE TRANSPOSE: 3 puts middle C at 48k
        float  attack     = 0.1f; // 0..1 each
        float  decay      = 0.4f;
        float  sustain    = 0.8f;
        float  release    = 0.4f;
        float  portamento = 0.0f;
    };

    void Init(float block_rate);

    void NoteOn(int note, int velocity);
    void NoteOff(int note);
    void PitchBend(int value); // 0..16383, 8192 centred
    void ModWheel(float amount) { mod_ = amount; }
    void Exit();

    bool Active() const { return active_; }

    // Once per block; then read the clock and the gain.
    void  Update(const Settings &s);
    float ClockHz() const { return hz_; }
    float Gain() const { return gain_; }
    int   Note() const { return held_ > 0 ? notes_[held_ - 1] : last_note_; }

  private:
    static constexpr int kMaxHeld = 8;

    bool  active_ = false;
    int   notes_[kMaxHeld];
    int   held_ = 0, last_note_ = 60;
    float velocity_ = 1.0f;
    float bend_ = 0.0f, mod_ = 0.0f; // semitones, 0..1
    float pitch_ = 60.0f;            // gliding, in notes
    float vibrato_phase_ = 0.0f;
    float env_ = 0.0f;
    enum class Stage
    {
        kIdle,
        kAttack,
        kDecay,
        kSustain,
        kRelease,
    } stage_ = Stage::kIdle;
    float block_rate_ = 12000.0f;
    float hz_ = 48000.0f, gain_ = 1.0f;
};

// Grainfall - Granules: Haze, Tunnel and Strum
//
// Haze: clusters of grains wash the sound out.
//   Activity - grain density and spread (stereo and in time)
//   Repeats  - feedback: grains of grains, up to a long regenerating wash
//   Shape    - each grain's contour, with soft edges
//   Time     - how far back grains reach (one step, 50 ms to 2 s)
//   A  long overlapping grains from a play head moving at half speed, so
//      the sound is stretched
//   B  many short grains scattered at random
//   C  normal and double-speed grains
//   D  normal and half-speed grains
//
// Tunnel: each new note captures a micro-loop that drones on. Up to four
// overlap; the oldest fades out to make room.
//   Activity - depth of the variation's modifier
//   Repeats  - how long the drones take to fade: 1 s to 30 s, and at the
//              top of the knob they don't fade at all
//   Shape    - contour of each pass round the loop
//   Time     - loop length: an eighth of a step, 20 ms to 400 ms
//   A  loop lengths slowly compress and stretch
//   B  an octave down, with low-pass sweeps
//   C  each drone through a resonant band-pass at its own frequency
//   D  each new note squeezes the loops short, then they relax back
//
// Strum: recent notes are replayed on the Time grid. Notes older than about
// 15 s are forgotten, as the buffer is about to record over them.
//   Activity - density: from one note every four steps to two per step
//              (B: how many copies overlap)
//   Repeats  - how long the repeats go on after you play a note: they fade
//              over 0.3 s to 12 s, and at the top of the knob never stop
//   Shape    - each note's contour
//   Time     - the grid, and the note length (one step, 80 ms to 600 ms)
//   A  repeats the most recent note
//   B  overlapping, slightly detuned copies of the most recent note phase
//      against each other
//   C  cascades through the last few notes in turn
//   D  as C, with double-speed grains of recent notes mixed in

#pragma once

#include "effect.h"

class FxHaze : public Effect
{
  public:
    void  Reset() override;
    void  Block(const EffectContext &ctx) override;
    float Feedback(float repeats) const override;

  private:
    float acc_ = 0.0f; // counts up to 1, then starts a grain
    float lag_ = 0.0f; // A: how far the stretched play head is behind, frames
};

class FxTunnel : public Effect
{
  public:
    void Reset() override;
    void Block(const EffectContext &ctx) override;

  private:
    static constexpr int kMaxDrones = 4;

    struct Drone
    {
        VoicePool::Handle voice  = 0;
        float             phase  = 0.0f; // modulation, 0..1
        float             centre = 0.0f; // C: band-pass frequency
    };

    void StartDrone(const EffectContext &ctx, float length);

    Drone    drones_[kMaxDrones];
    int      next_    = 0;
    bool     pending_ = false; // a note is waiting to be recorded in full
    uint32_t pending_start_ = 0;
    float    squeeze_ = 0.0f;  // D: 1 just after a note, relaxing to 0
};

class FxStrum : public Effect
{
  public:
    void Reset() override;
    void Block(const EffectContext &ctx) override;

  private:
    // Where the i-th most recent note starts, and how much of it is
    // recorded so far. False if there's none.
    bool Note(const EffectContext &ctx, int i, uint32_t *start, float *recorded) const;
    void Play(const EffectContext &ctx, uint32_t start, float length, float rate,
              float gain, float pan);

    int32_t last_tick_ = -1;
    int     chain_     = 0;    // C, D: position in the cascade
    float   level_     = 0.0f; // 1 after a new note, fading with each repeat
};

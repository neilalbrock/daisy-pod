// Grainfall - Micro Loop: Mosaic, Seq and Glide
//
// Loops of the last Time step, locked to the grid. A loop at 2x reads two
// steps' worth of audio in one step and one at 0.5x half a step, so every
// layer repeats once per step whatever its speed. Repeats sets how long a
// loop is kept before fresh audio replaces it: 1 to 32 steps (never more
// than about 15 s, as the buffer is about to record over it by then).
//
// Mosaic: overlapping loops at several speeds.
//   Activity - how many loopers play, 1 to 4. They take turns to capture.
//   Shape    - contour of each pass
//   A  normal and double speed (an octave up)
//   B  normal and half speed (an octave down)
//   C  all double speed
//   D  half, normal, double and quadruple speed
//
// Seq: steps of recent audio rearranged into an 8-step sequence, which
// repeats until fresh material replaces it (Repeats, counted in steps).
//   Activity - per variation, below
//   Shape    - contour of each slice
//   A  shuffled slices with rests; Activity varies each slice's low-pass
//   B  slices bounce between normal and half speed (Activity: how often);
//      at full Activity a soft half-speed pad sits under the sequence
//   C  overlapping two-step slices in up to three layers (Activity), with a
//      low-pass sweep over each sequence
//   D  interlocking layers on the half-step (Activity); at full, a
//      bit-crushed octave-down layer joins
//
// Glide: two overlapping loops whose speed glides between two settings.
//   Activity - glide rate, one sweep per 16 beats to two per beat
//   Shape    - the glide's path: square jumps between the speeds, ramp up
//              rises and drops back, triangle rises and falls, ramp down
//              falls and jumps back
//   A  between half and normal speed
//   B  between double and half speed
//   C  between normal and double speed
//   D  one loop glides up while the other glides down, half to double

#pragma once

#include "effect.h"

// Shared by the three: steps counted since the effect started, so hold
// lengths don't stumble when the tempo's position wraps.
class MicroLoopBase : public Effect
{
  protected:
    // Steps between recaptures for a Repeats setting.
    static int HoldSteps(float repeats);

    // Loop length (a step) in output frames.
    static float LoopFrames(const EffectContext &ctx);

    // True if a slice starting at `start` is too old to trust: the buffer
    // is about to record over it.
    static bool Stale(const EffectContext &ctx, uint32_t start);

    uint32_t steps_ = 0;
};

class FxMosaic : public MicroLoopBase
{
  public:
    void Reset() override;
    void Block(const EffectContext &ctx) override;

  private:
    static constexpr int kMaxLoopers = 4;

    struct Looper
    {
        VoicePool::Handle voice = 0;
        uint32_t          start = 0;
    };
    Looper loopers_[kMaxLoopers];
};

class FxSeq : public MicroLoopBase
{
  public:
    void Reset() override;
    void Block(const EffectContext &ctx) override;

  private:
    static constexpr int kSteps  = 8;
    static constexpr int kLayers = 3;

    struct Slot
    {
        uint32_t start[kLayers]; // one source per layer
        bool     rest;
        float    rate;
        float    cutoff; // A: per-slice low-pass, 0 = open
    };

    void Generate(const EffectContext &ctx, float loop);
    void PlayStep(const EffectContext &ctx, float loop);

    Slot              pattern_[kSteps] = {};
    Slot              offbeat_[kSteps] = {}; // D's second layer
    int               pos_          = 0;
    int               held_         = 0; // steps since the pattern was made
    bool              have_pattern_ = false;
    int32_t           last_half_    = -1;
    VoicePool::Handle sweep_[6]     = {}; // C: voices following the sweep
    int               next_sweep_   = 0;
};

class FxGlide : public MicroLoopBase
{
  public:
    void Reset() override;
    void Block(const EffectContext &ctx) override;

  private:
    struct Looper
    {
        VoicePool::Handle voice = 0;
        uint32_t          start = 0;
    };
    Looper loopers_[2];
    float  phase_ = 0.0f; // glide, 0..1
};

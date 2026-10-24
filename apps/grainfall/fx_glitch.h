// Grainfall - Glitch: Blocks, Interrupt and Arp
//
// Blocks: each note you play sets off a burst of blocks, half-step slices of
// the last two steps played back rearranged on the grid.
//   Activity - how far the blocks stray from a steady, repeating order:
//              reshuffles, stutters and each variation's manipulation
//   Repeats  - how long a burst goes on: 2 to 32 half-steps, and at the top
//              of the knob it never stops
//   Shape    - each block's contour
//   A  bursts of quick runs through the last few notes break in
//   B  blocks pitch-shifted: an octave down, a fifth or an octave up
//   C  longer blocks overlap, filtered and fading out: softer
//   D  pitch-shifted and bit-crushed
//
// Interrupt: glitches cut into the live signal, which the effect passes
// through between them. The dry signal is not touched, so at 100% Mix the
// glitches truly interrupt the input.
//   Activity - how much each glitch is chopped up and manipulated
//   Repeats  - how often glitches come: from a rare one to every other step
//   Shape    - each chop's contour
//   A  rearranged chops of your playing
//   B  chops pitch-shifted
//   C  a slice echoing through a falling band-pass sweep
//   D  bit-crushed, with drastic speeds and tiny stutters
//
// Arp: the last few notes you played, sequenced two to a step.
//   Activity - steps in the arpeggio, 2 to 8 (the recent notes in turn,
//              round again if there are fewer)
//   Repeats  - how long it plays on after a note: it fades over 0.3 s to
//              12 s, and at the top of the knob never stops
//   Shape    - each note's contour
//   A  the notes as played
//   B  each step at its own speed, so the pattern climbs and falls a major
//      arpeggio
//   C  each step through its own random filter
//   D  each step bit-crushed by its own amount

#pragma once

#include "effect.h"

class FxBlocks : public Effect
{
  public:
    void Reset() override;
    void Block(const EffectContext &ctx) override;

  private:
    static constexpr int kPattern = 8;
    static constexpr int kRun     = 4; // notes in an A run

    void PlayBlock(const EffectContext &ctx, float tick);

    int      back_[kPattern] = {}; // blocks back from the anchor
    uint32_t anchor_         = 0;  // write head when the blocks were taken
    int      pos_            = 0;
    int      left_           = 0; // ticks left in the burst, < 0 = endless
    int32_t  last_tick_      = -1;
    int32_t  last_quarter_   = -1;
    int      run_left_       = 0; // A: notes left in the current run
    bool     skip_           = false;
};

class FxInterrupt : public Effect
{
  public:
    void Reset() override;
    void Block(const EffectContext &ctx) override;
    void Process(const SampleBuffer &buf, float in_l, float in_r, float *l,
                 float *r, float *fb_l, float *fb_r) override;

  private:
    void Start(const EffectContext &ctx);
    void Chop(const EffectContext &ctx);

    int32_t last_step_ = -1;
    float   left_      = 0.0f; // frames left in this glitch
    float   length_    = 0.0f; // the whole glitch
    float   chop_      = 0.0f; // chop length, frames
    float   next_chop_ = 0.0f; // frames into the glitch for the next chop
    float   pos_       = 0.0f; // frames into the glitch
    VoicePool::Handle sweep_ = 0; // C

    float duck_ = 0.0f, duck_target_ = 0.0f, duck_coef_ = 0.0f;
};

class FxArp : public Effect
{
  public:
    void Reset() override;
    void Block(const EffectContext &ctx) override;

  private:
    static constexpr int kMaxSteps = 8;

    void Roll(const EffectContext &ctx);

    int32_t last_tick_ = -1;
    int     pos_       = 0;
    float   level_     = 0.0f; // 1 after a new note, fading with each step

    // C, D: each step's filter or crush, rolled with each new note.
    float cutoff_[kMaxSteps] = {};
    float q_[kMaxSteps]      = {};
    bool  band_[kMaxSteps]   = {};
    int   bits_[kMaxSteps]   = {};
    int   hold_[kMaxSteps]   = {};
};

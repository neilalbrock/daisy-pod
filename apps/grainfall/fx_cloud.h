// Grainfall - placeholder grain cloud
//
// Stands in for every effect slot until the real effects are written, so
// the controls, tempo, Hold and reverse can be tried out. Grains start on
// each subdivision step and at random in between:
//
//   Activity  - density, 2 to 60 grains a second on top of the steps
//   Repeats   - grain length, 20 ms to 1 s
//   Shape     - grain envelope
//   Variation - playback speeds: A 1x, B 1x + 2x, C 1x + 0.5x, D all three
//   Time      - the step grid, and how far back grains reach (one step)

#pragma once

#include "effect.h"

class FxCloud : public Effect
{
  public:
    void Reset() override { acc_ = 0.0f; }
    void Block(const EffectContext &ctx) override;

  private:
    void Spawn(const EffectContext &ctx, float size_frames, float gain);

    float acc_ = 0.0f; // counts up to 1, then starts a grain
};

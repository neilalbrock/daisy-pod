// Wavebyte - a low-frequency oscillator
//
// The Protein's two LFOs: seven shapes, a Speed or, Clocked, a Ratio of the
// tempo from 4 bars to 1/64 (dotted and triplet values too). In Free Voice
// mode every note starts its own LFO from the beginning of its cycle; Free
// Global and Clocked run one LFO that every voice shares (Clocked always
// does, as on the Protein). Random glides from one random value to the
// next each cycle; S & H jumps.
//
// Advance() once per block. libDaisy-free.

#pragma once

#include <cmath>
#include <cstdint>

#include "dsp.h"

class Lfo
{
  public:
    enum Shape
    {
        kSine,
        kTriangle,
        kPulse,
        kSaw, // falling
        kSawUp,
        kRandom,
        kSampleHold,
        kNumShapes,
    };

    enum Mode
    {
        kFreeVoice,
        kFreeGlobal,
        kClocked,
        kNumModes,
    };

    // Ratios in beats, longest first: 4/1 (four bars) to 1/64.
    static constexpr int   kNumRatios              = 22;
    static constexpr float kRatioBeats[kNumRatios] = {
        16.0f,        12.0f, 8.0f, 6.0f,         4.0f,        3.0f,         8.0f / 3.0f, 2.0f,
        1.5f,         4.0f / 3.0f, 1.0f, 0.75f, 2.0f / 3.0f, 0.5f,       0.375f,      1.0f / 3.0f,
        0.25f,        0.1875f, 1.0f / 6.0f, 0.125f, 1.0f / 12.0f, 0.0625f,
    };

    // Speed: 0.02 Hz to 41 Hz across the knob.
    static float SpeedHz(float x) { return 0.02f * exp2f(11.0f * x); }

    void Seed(uint32_t seed)
    {
        rng_.Seed(seed);
        from_ = rng_.Bi();
        to_   = rng_.Bi();
        held_ = to_;
    }

    // Back to the start of a cycle, with new random values.
    void Restart()
    {
        phase_ = 0.0f;
        NewCycle();
    }

    // Moves on by `cycles` and returns the value, -1..1.
    float Advance(float cycles, Shape shape)
    {
        phase_ += cycles;
        if(phase_ >= 1.0f)
        {
            phase_ -= floorf(phase_);
            NewCycle();
        }
        return Value(shape);
    }

  private:
    void NewCycle()
    {
        from_ = to_;
        to_   = rng_.Bi();
        held_ = to_;
    }

    float Value(Shape shape) const
    {
        float p = phase_;
        switch(shape)
        {
            case kSine: return dsp::FastSin(p);
            case kTriangle: return p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f);
            case kPulse: return p < 0.5f ? 1.0f : -1.0f;
            case kSaw: return 1.0f - 2.0f * p;
            case kSawUp: return 2.0f * p - 1.0f;
            case kRandom: return dsp::Lerp(from_, to_, p * p * (3.0f - 2.0f * p));
            case kSampleHold:
            default: return held_;
        }
    }

    dsp::Rng rng_;
    float    phase_ = 0.0f;
    float    from_ = 0.0f, to_ = 0.0f, held_ = 0.0f;
};

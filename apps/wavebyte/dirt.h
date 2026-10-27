// Wavebyte - Dirt, a voice's noise source
//
// The Protein's Dirt: noises and clicks into the filter, beside the
// oscillators, at the Dirt level.
//
//   Static    white noise
//   Crackle   short random pulses, many small and a few loud, like a record
//   Geiger    sparse random ticks, like a Geiger counter
//   Click     one short noise pulse as each note starts
//   Burst 1-3 one noise burst as each note starts: 10, 40 and 150 ms
//   Ring Mod  osc 1 times osc 2, at their full level whatever their Level
//             knobs say, so turning both down leaves only the ring
//             modulation
//
// Trigger() as a note starts, Process() per sample. libDaisy-free.

#pragma once

#include <cmath>
#include <cstdint>

#include "dsp.h"

class Dirt
{
  public:
    enum Type
    {
        kStatic,
        kCrackle,
        kGeiger,
        kClick,
        kBurst1,
        kBurst2,
        kBurst3,
        kRingMod,
        kNumTypes,
    };

    void Init(float sample_rate, uint32_t seed)
    {
        rng_.Seed(seed);
        // Chances per sample, as fractions of 2^32.
        crackle_chance_ = (uint32_t)(kCrackleHz / sample_rate * 4294967296.0f);
        geiger_chance_  = (uint32_t)(kGeigerHz / sample_rate * 4294967296.0f);
        const float tau_s[] = {0.00007f, 0.0004f, 0.001f, 0.010f / 3.0f, 0.040f / 3.0f, 0.150f / 3.0f};
        for(int i = 0; i < 6; i++)
            decay_[i] = expf(-1.0f / (tau_s[i] * sample_rate));
    }

    // As a note starts: Click and the Bursts fire.
    void Trigger(Type type)
    {
        if(type >= kClick && type <= kBurst3)
        {
            env_  = 1.0f;
            coef_ = decay_[2 + (type - kClick)];
        }
    }

    // A sample at full level. ring: osc 1 times osc 2, each -1..1.
    float Process(Type type, float ring)
    {
        switch(type)
        {
            case kStatic: return 0.5f * rng_.Bi();
            case kCrackle:
            {
                // Mostly faint ticks; the cube makes the loud ones rare.
                if(rng_.Next() < crackle_chance_)
                {
                    float a = rng_.Uni();
                    env_    = a * a * a;
                    sign_   = (rng_.Next() & 1u) ? 1.0f : -1.0f;
                    coef_   = decay_[0];
                }
                env_ *= coef_;
                return env_ * (0.6f * sign_ + 0.4f * rng_.Bi());
            }
            case kGeiger:
                if(rng_.Next() < geiger_chance_)
                {
                    env_  = 0.8f;
                    coef_ = decay_[1];
                }
                env_ *= coef_;
                return env_ * rng_.Bi();
            case kRingMod: return ring;
            default: // Click and the Bursts
                env_ *= coef_;
                return env_ * rng_.Bi();
        }
    }

  private:
    static constexpr float kCrackleHz = 120.0f; // pulses a second, most of them faint
    static constexpr float kGeigerHz  = 6.0f;

    dsp::Rng rng_;
    uint32_t crackle_chance_ = 0, geiger_chance_ = 0;
    float    decay_[6] = {}; // per sample: crackle, geiger, click, bursts 1-3
    float    env_ = 0.0f, coef_ = 0.0f, sign_ = 1.0f;
};

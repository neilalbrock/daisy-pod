// Wavebyte - ADSR envelope, stepped once per block
//
// Attack rises in a straight line, decay and release fall exponentially, as
// on the Microwave. A new note attacks from wherever the envelope is, so a
// stolen or retriggered voice doesn't click. The engine works out the
// coefficients once per block for every voice (Coefs::Set), since the
// stage times are the patch's.
//
// libDaisy-free.

#pragma once

#include <cmath>

#include "dsp.h"

class Adsr
{
  public:
    struct Coefs
    {
        float attack_step; // per block
        float decay;       // one-pole coefficient per block
        float sustain;     // level
        float release;

        // Times from the 0..1 settings: attack 0.5 ms to 10 s, decay and
        // release 1 ms to 20 s, each exponential across the knob.
        static float AttackSeconds(float x) { return 0.0005f * powf(20000.0f, x); }
        static float FallSeconds(float x) { return 0.001f * powf(20000.0f, x); }

        void Set(float attack, float decay_x, float sustain_x, float release_x, float block_rate)
        {
            SetTimes(AttackSeconds(attack), FallSeconds(decay_x), sustain_x,
                     FallSeconds(release_x), block_rate);
        }

        // From times in seconds. Cheap enough (no expf) for a voice to
        // work out its own each block when the mod matrix moves them.
        void SetTimes(float attack_s, float decay_s, float sustain_x, float release_s, float block_rate)
        {
            attack_step = 1.0f / (attack_s * block_rate);
            decay       = Fall(decay_s, block_rate);
            sustain     = dsp::Clamp(sustain_x, 0.0f, 1.0f);
            release     = Fall(release_s, block_rate);
        }

        // Most of the way (95%) in `seconds`: 1 - e^(-3 / blocks).
        static float Fall(float seconds, float block_rate)
        {
            return 1.0f - dsp::FastExp2(-4.328085f / (seconds * block_rate));
        }
    };

    void Gate(bool on)
    {
        if(on)
            stage_ = kAttack;
        else if(stage_ != kIdle)
            stage_ = kRelease;
    }

    void Reset()
    {
        stage_ = kIdle;
        level_ = 0.0f;
    }

    float Process(const Coefs &c)
    {
        switch(stage_)
        {
            case kAttack:
                level_ += c.attack_step;
                if(level_ >= 1.0f)
                {
                    level_ = 1.0f;
                    stage_ = kDecay;
                }
                break;
            case kDecay: level_ += (c.sustain - level_) * c.decay; break;
            case kRelease:
                level_ -= level_ * c.release;
                if(level_ < 1e-4f)
                    Reset();
                break;
            case kIdle: break;
        }
        return level_;
    }

    float Level() const { return level_; }
    bool  Idle() const { return stage_ == kIdle; }
    bool  Released() const { return stage_ == kRelease || stage_ == kIdle; }

  private:
    enum Stage
    {
        kIdle,
        kAttack,
        kDecay, // and sustain
        kRelease,
    };

    Stage stage_ = kIdle;
    float level_ = 0.0f;
};

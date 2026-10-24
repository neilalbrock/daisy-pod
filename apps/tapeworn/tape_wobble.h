// Tapeworn - wow and flutter generators
//
// Both produce a modulation signal of roughly -1..1 that the engine scales
// into a delay-time offset. Wow is slow and wandering (an off-centre reel, a
// stretched belt); flutter is fast and steady (the capstan and pinch roller).

#pragma once

#include <cstdint>

#include "dsp.h"

class Wow
{
  public:
    void  Init(float sample_rate, uint32_t seed);
    float Process();

  private:
    float     sample_rate_ = 48000.0f;
    dsp::Rng  rng_;
    float     target_ = 0.0f, s1_ = 0.0f, s2_ = 0.0f; // smoothed random walk
    float     coef_   = 0.0f;
    int32_t   hold_   = 0; // samples until the next random target
    float     phase_  = 0.0f, inc_ = 0.0f; // steady once-per-revolution part
};

class Flutter
{
  public:
    void  Init(float sample_rate, uint32_t seed);
    float Process();

  private:
    float     sample_rate_ = 48000.0f;
    dsp::Rng  rng_;
    float     phase1_ = 0.0f, phase2_ = 0.0f;
    float     inc1_ = 0.0f, inc2_ = 0.0f;
    float     drift_ = 1.0f, drift_target_ = 1.0f, drift_coef_ = 0.0f;
    int32_t   drift_hold_ = 0;
    float     noise_[4] = {}; // four one-poles in series on white noise
    float     noise_coef_ = 0.0f;
};

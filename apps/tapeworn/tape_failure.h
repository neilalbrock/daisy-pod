// Tapeworn - tape failures
//
// Random, short-lived malfunctions whose rate and severity follow the
// FAILURE amount:
//
//   Crinkles - bursts of tiny clicks from creased tape. The rustle is heard,
//              and the signal crumbles: small, fast level and pitch jitter.
//   Pops     - single clicks from debris, with a brief dip in level.
//   Snags    - the tape catches for a moment: a sharp pitch dip and recovery.
//   Drops    - the head loses contact: a short gap of near silence.
//
// Crinkles and pops start first; snags join past ~15 % and drops past ~30 %.
// One instance per channel, seeded differently, gives the stereo spread.

#pragma once

#include <cstdint>

#include "dsp.h"

class TapeFailure
{
  public:
    struct Out
    {
        float gain;  // multiply the signal by this
        float delay; // extra tape delay, samples
        float sound; // crinkle and pop noise to add before the tape model
    };

    void Init(float sample_rate, uint32_t seed);

    // amount 0..1. extra 0..1 pushes towards complete malfunction (the AUX
    // fail effect): full amount and twice the event rate. Once per block.
    void SetAmount(float amount, float extra);

    Out Process();

  private:
    void StartCrinkle();

    float     sample_rate_ = 48000.0f;
    dsp::Rng  rng_;

    float amount_      = 0.0f;
    float crinkle_hz_  = 0.0f;
    float pop_hz_      = 0.0f;
    float snag_hz_     = 0.0f;
    float drop_hz_     = 0.0f;
    float sound_level_ = 0.0f;

    // Crinkle burst
    int32_t crinkle_left_    = 0; // samples left in the burst
    float   crinkle_density_ = 0.0f; // clicks per second
    float   crinkle_depth_   = 0.0f;
    float   crumble_ = 1.0f, crumble_target_ = 1.0f, crumble_coef_ = 0.0f;
    float   jitter_ = 0.0f, jitter_target_ = 0.0f, jitter_coef_ = 0.0f;

    // Pop: the level dip after a pop
    int32_t pop_dip_left_ = 0;
    float   pop_dip_      = 1.0f, pop_dip_target_ = 1.0f;

    // Snag: one raised-cosine bump in delay
    float snag_phase_ = 1.0f, snag_inc_ = 0.0f, snag_amp_ = 0.0f;

    // Drop
    int32_t drop_left_ = 0;
    float   drop_ = 1.0f, drop_target_ = 1.0f, drop_coef_ = 0.0f;

    // Click shaping: a high-passed rustle and a resonant low pop
    dsp::Biquad rustle_filter_, pop_filter_;
};

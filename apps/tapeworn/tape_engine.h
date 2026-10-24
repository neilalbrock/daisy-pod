// Tapeworn - tape engine
//
// Signal flow, per channel:
//
//   in -> hysteresis -> tape delay line (wow, flutter, snags, tape stop)
//      -> + hiss, crinkles and pops -> tape model EQ
//      -> drops and level jitter -> + mechanical hum -> wet out
//
// Dry mix and output volume are left to the caller. The delay buffers are
// supplied by the caller (they belong in SDRAM) and Process() never
// allocates, so it is safe to call from the audio callback.

#pragma once

#include <cstddef>
#include <cstdint>

#include "dsp.h"
#include "hysteresis.h"
#include "tape_failure.h"
#include "tape_model.h"
#include "tape_wobble.h"

class TapeEngine
{
  public:
    enum class Aux
    {
        kStop,   // tape stop: the wet signal slows to a halt
        kFail,   // failure pushed to complete malfunction
        kFilter, // tape model bypassed
    };
    static constexpr int kNumAux = 3;

    struct Params
    {
        float wow      = 0.0f; // 0..1
        float flutter  = 0.0f; // 0..1
        float saturate = 0.0f; // 0..1
        float failure  = 0.0f; // 0..1
        int   model    = 0;    // 0 (flat, no EQ) .. TapeModel::kNumModels - 1
        float noise    = 0.0f; // 0..1, hiss and hum
        float spread   = 0.0f; // 0 = channels move together, 1 = independent
        Aux   aux      = Aux::kStop;
        bool  aux_on   = false;
    };

    // buf_l/buf_r: buffer_len samples each; buffer_len must be a power of
    // two, and at least a second long for the tape stop.
    void Init(float sample_rate, float *buf_l, float *buf_r, size_t buffer_len);

    // Once per block from the audio callback; values are clamped.
    void SetParams(const Params &p);

    void Process(float in_l, float in_r, float *out_l, float *out_r);

  private:
    struct Channel
    {
        float      *buf = nullptr;
        Hysteresis  hysteresis;
        dsp::DcBlock dc;
        Wow         wow;
        Flutter     flutter;
        TapeFailure failure;
        TapeModel   model;
        dsp::Rng    hiss_rng;
    };

    float Read(const float *buf, float delay) const;

    float  sample_rate_ = 48000.0f;
    size_t len_         = 0;
    size_t mask_        = 0;
    size_t write_       = 0;

    Params  params_;
    Channel ch_[2];

    // Smoothed per sample, in samples of delay
    float wow_depth_ = 0.0f, flutter_depth_ = 0.0f, centre_ = 0.0f;
    float wow_depth_target_ = 0.0f, flutter_depth_target_ = 0.0f;
    float centre_target_ = 0.0f;
    float depth_coef_ = 0.0f, centre_coef_ = 0.0f;
    float flutter_am_ = 0.0f;
    float spread_     = 0.0f;
    float hiss_level_ = 0.0f, hum_level_ = 0.0f;

    // Aux ramps, 0..1, each heading for 1 while its effect is engaged
    float stop_pos_ = 0.0f, fail_amt_ = 0.0f, filter_amt_ = 0.0f;
    float stop_inc_ = 0.0f, start_inc_ = 0.0f, fail_inc_ = 0.0f;
    float filter_inc_ = 0.0f;

    // Tape stop: extra delay built up while the tape runs slow, and the
    // crossfade that drops it again once the tape is back up to speed.
    float excess_ = 0.0f, max_excess_ = 0.0f;
    float catchup_excess_ = 0.0f, catchup_ = 1.0f, catchup_inc_ = 0.0f;

    float hum_phase_ = 0.0f, hum_inc_ = 0.0f;
};

// Grainfall - post-effect chain
//
//   wet -> pitch modulation -> resonant low-pass -> effect volume
//
// Pitch modulation is a short delay swept by a sine, the two channels a
// quarter cycle apart: a gentle chorus-like shimmer at low depth, a seasick
// wobble at high. The low-pass is fully open at the top of its knob and
// silences the effect at the bottom. Resonance eases off over the top tenth
// of the filter's travel, so fully open really is open. libDaisy-free.

#pragma once

#include <cstddef>

#include "dsp.h"

class PostFx
{
  public:
    struct Params
    {
        float mod_depth = 0.0f; // 0..1
        float mod_rate  = 0.3f; // 0..1
        float filter    = 1.0f; // 0 = closed (silent) .. 1 = open
        float resonance = 0.2f; // 0..1
        float volume    = 0.5f; // 0..1, noon is unity, max x2
    };

    static constexpr size_t kModLen = 1024; // 21 ms, a power of two

    // mod_buffer: 2 * kModLen floats, the pitch mod delay (SDRAM is fine).
    void Init(float sample_rate, float *mod_buffer);

    // Once per block.
    void SetParams(const Params &p);

    void Process(float *l, float *r);

  private:
    float ReadMod(const float *buf, float delay) const;

    float sample_rate_ = 48000.0f;

    float *mod_buf_l_ = nullptr, *mod_buf_r_ = nullptr;
    size_t mod_write_ = 0;
    float  lfo_phase_ = 0.0f, lfo_inc_ = 0.0f;
    float  depth_ = 0.0f, depth_target_ = 0.0f; // swing in samples
    float  depth_coef_ = 0.0f;

    dsp::Svf svf_l_, svf_r_;
    float    cutoff_ = 20000.0f; // smoothed per block
    float    block_coef_ = 0.0f;
    float    gain_ = 1.0f, gain_target_ = 1.0f, gain_coef_ = 0.0f;
};

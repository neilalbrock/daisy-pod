// Stormsea - the engine
//
//   in -+------------------------------------------------------- dry -+
//       |                                                             |
//       +-> gate (on/off, Freeze) -> low cut -> predelay              |
//           -> the sea (sea.h): 8 lines, Size, Decay, Tone, Depth     |
//           -> Width -> Tone's output low-pass (at ∞ and frozen)      |
//           -> Tide (level and position swells) -> wet                |
//       mix (equal power; Kill dry drops the dry) <-------------------+
//
// The calm sea, stage 1 of the roadmap in README.md: the storm will go
// between the sea and the wet output, and into the sea's loop.
//
// The gate closes the sea's input when off (the tail rings out with trails
// on, and is cut in 5 ms with them off) and while frozen. Decay's top
// stretch and Freeze hold the sea for good; both ease in and back out, so
// nothing jumps, and while it's held Tone's damping moves out of the loop
// into a low-pass on the output, so a held sea keeps its colour.
//
// libDaisy-free: main.cpp sets Params once per block and calls Process()
// once per sample.

#pragma once

#include <cstddef>
#include <cstdint>

#include "dsp.h"
#include "sea.h"
#include "wander.h"

class StormEngine
{
  public:
    enum Width
    {
        kNarrow,
        kMedium,
        kWide,
        kNumWidths,
    };

    static constexpr int   kNumPredelays              = 6;
    static constexpr float kPredelayMs[kNumPredelays] = {0.0f, 15.0f, 30.0f, 60.0f, 120.0f, 240.0f};

    // Decay from here up is ∞.
    static constexpr float kInfinite = 0.96f;

    struct Params
    {
        float decay    = 0.42f; // 0..kInfinite: 0.5 s to 60 s; above: ∞
        float mix      = 0.35f; // 0 dry .. 1 wet
        float size     = 0.7f;  // 0..1: 30% to 100% of the lines' lengths
        float tone     = 0.6f;  // 0 dark .. 1 bright
        float depth    = 0.25f; // 0..1, the lines' swing
        float tide     = 0.0f;  // 0..1, the swells
        int   width    = kWide;
        int   predelay = 1; // index into kPredelayMs
        bool  active   = true;
        bool  trails   = true;
        bool  freeze   = false;
        bool  kill_dry = false;
    };

    // For the web page and the LEDs.
    struct Probe
    {
        float level; // the sea's mean square a line
        float tide;  // the level swell now, dB
        float rt60;  // the decay now, s (1e6: held)
        float hold;  // 0..1: how far the sea is held (∞ or frozen)
    };

    // The memory Init needs, in floats: fast (RAM_D2) for the sea, and
    // slow (SDRAM) for the predelay.
    static constexpr size_t kFastFloats     = Sea::kFloats;
    static constexpr size_t kPredelayFrames = 16384; // a power of two, over 240 ms
    static constexpr size_t kSlowFloats     = 2 * kPredelayFrames;

    // The decay a Decay setting gives, in seconds, or 0 for ∞.
    static float DecaySeconds(float decay);

    // Init clears both.
    void Init(float sample_rate, size_t block_size, float *fast_mem, float *slow_mem);

    // Once per block.
    void SetParams(const Params &p);

    void Process(float in_l, float in_r, float *out_l, float *out_r);

    Probe GetProbe() const;

    // The wet output's peak since the last call.
    float TakeWetPeak()
    {
        float peak = wet_peak_;
        wet_peak_  = 0.0f;
        return peak;
    }

  private:
    float sample_rate_ = 48000.0f;

    Sea         sea_;
    Wander      tide_level_, tide_pan_;
    dsp::Biquad low_cut_[2];
    dsp::Svf    lp_[2];

    // The predelay: interleaved stereo in SDRAM. A change of length fades
    // from the old read to the new over 30 ms.
    float   *pre_     = nullptr;
    uint32_t pre_pos_ = 0;
    int      pre_index_ = -1;
    float    pre_from_ = 0.0f, pre_to_ = 0.0f; // frames
    float    pre_fade_ = 1.0f, pre_step_ = 0.0f;

    // Smoothing coefficients per block, for the time constants they're named
    // after in SetParams().
    struct
    {
        float fast, size, hold, cutoff, mix, rt_up, rt_down, rt_cut;
    } coef_ = {};

    // Smoothed once per block.
    float size_ = 0.7f, log_rt60_ = 0.0f, hold_ = 0.0f, depth_ = 0.0f, tide_ = 0.0f;
    float out_side_ = 1.0f, in_side_ = 1.0f, width_gain_ = 1.0f;
    float log_cutoff_ = 9.9f, cutoff_set_ = 0.0f;
    float mix_ = 0.35f, dry_ = 1.0f, wet_ = 0.0f;
    float tide_l_ = 1.0f, tide_r_ = 1.0f, tide_db_ = 0.0f;

    // Per sample: ramps of 5 ms (bypass, cut, kill dry) and 20 ms (the gate).
    float gate_ = 1.0f, gate_target_ = 1.0f, gate_step_ = 0.0f;
    float bypass_ = 0.0f, bypass_target_ = 0.0f; // 1: the dry at unity
    float cut_ = 0.0f, cut_target_ = 0.0f;       // 1: the wet cut (no trails)
    float kill_ = 0.0f, kill_target_ = 0.0f;     // 1: no dry at all
    float fade_step_ = 0.0f;

    float wet_peak_ = 0.0f;
};

// Stormsea - the engine
//
//   in -+------------------------------------------------------- dry -+
//       |                                                             |
//       +-> gate (on/off, Freeze) -> low cut -> predelay              |
//           [+ what the sky drops in]                                 |
//           -> the sea (sea.h): 8 lines, Size, Decay, Tone, Depth,    |
//              [Wind's turbulence, Surge, Undertow]                   |
//              -> [the sky's tail ring]                               |
//           -> Width -> Tone's output low-pass (at ∞ and frozen)      |
//           [-> Wind's whistle, + the sky (sky.h): Rain, Lightning,   |
//            Thunder] -> Tide (level and position) -> wet             |
//       mix (equal power; Kill dry drops the dry) <-------------------+
//
// The calm sea, and the storm in brackets. Each block works out the storm's strength from
// moment to moment: Storm, swung by Gusts (slow fronts and sudden gusts,
// both Wanders), eased down to nothing while Calm is held and up to full
// while Squall is. Each element's own setting x the strength is how much
// of it there is now; at Storm 0 there's none.
//
// Wind: each line's length wanders at random, deeper and faster as it
// rises, so the tail's pitch churns; and a resonant band-pass on the tail,
// its centre swept by a Wander and lifted by the gusts, whistles.
// Surge: stretches the decay towards ∞ and pushes the sea's gain past
// unity into its clipper, so the tail swells and boils; as the storm
// drops it settles back to its Decay.
// Rain, Lightning and Thunder: the sky plays slices of the sea's own tail
// back into it and into the wet.
// Undertow: the sea's lines turn into each other and its feed sinks.
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
#include "sky.h"
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

        // The storm
        float storm     = 0.0f; // 0 calm .. 1 gale
        float gusts     = 0.4f; // 0 steady .. 1 fronts and gusts
        float wind      = 0.5f; // each element's part in the storm, 0..1
        float rain      = 0.3f;
        float lightning = 0.3f;
        float thunder   = 0.5f;
        float surge     = 0.4f;
        float undertow  = 0.2f;
        int   strike    = Sky::kFlash;
        bool  calm      = false; // held: the storm dies down
        bool  squall    = false; // held: the storm rises to full
    };

    // For the web page and the LEDs.
    struct Probe
    {
        float level; // the sea's mean square a line
        float tide;  // the level swell now, dB
        float rt60;  // the decay now, s (1e6: held)
        float hold;  // 0..1: how far the sea is held (∞ or frozen)
        float storm; // the storm's strength now, 0..1
        float flash;   // 1 at a lightning strike, falling away
        float wind;    // each element now, 0..1: its setting x the storm
        float rain;
        float thunder; // the thunder rolling now
    };

    // The memory Init needs, in floats: fast (RAM_D2) for the sea, and
    // slow (SDRAM) for the predelay and the sky's tail ring.
    static constexpr size_t kFastFloats     = Sea::kFloats;
    static constexpr size_t kPredelayFrames = 16384; // a power of two, over 240 ms
    static constexpr size_t kSlowFloats     = 2 * kPredelayFrames + Sky::kFloats;

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
    Sky         sky_;
    Wander      tide_level_, tide_pan_;
    Wander      fronts_, gust_, whistle_sweep_;
    dsp::Biquad low_cut_[2];
    dsp::Svf    lp_[2], whistle_bp_[2];

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
        float calm, calm_back, squall, squall_back;
        float in_rise, in_fall;
    } coef_ = {};

    // Smoothed once per block.
    float size_ = 0.7f, log_rt60_ = 0.0f, hold_ = 0.0f, depth_ = 0.0f, tide_ = 0.0f;
    float out_side_ = 1.0f, in_side_ = 1.0f, width_gain_ = 1.0f;
    float log_cutoff_ = 9.9f, cutoff_set_ = 0.0f;
    float mix_ = 0.35f, dry_ = 1.0f, wet_ = 0.0f;
    float tide_l_ = 1.0f, tide_r_ = 1.0f, tide_db_ = 0.0f;
    float in_gain_ = 1.0f; // the sea's input, for the decay
    float in_energy_ = 0.0f, in_level_ = 0.0f, block_scale_ = 0.25f; // the input's level, for the wall
    float storm_knob_ = 0.0f, calm_ = 1.0f, squall_ = 0.0f, strength_ = 0.0f, wind_ = 0.0f;
    float whistle_mix_ = 0.0f, whistle_set_ = 0.0f; // the whistle's level, and the centre last set

    // Per sample: ramps of 5 ms (bypass, cut, kill dry) and 20 ms (the gate).
    float gate_ = 1.0f, gate_target_ = 1.0f, gate_step_ = 0.0f;
    float bypass_ = 0.0f, bypass_target_ = 0.0f; // 1: the dry at unity
    float cut_ = 0.0f, cut_target_ = 0.0f;       // 1: the wet cut (no trails)
    float kill_ = 0.0f, kill_target_ = 0.0f;     // 1: no dry at all
    float fade_step_ = 0.0f;

    float wet_peak_ = 0.0f;
};

// Slowdrift - the engine
//
//   in -+---------------------------------------------------------- dry --+
//       |                                                                 |
//       +-> gate (bypass, freeze) x Rise's swell                          |
//           -> ClockDomain at Stretch's rate (48 kHz down to 12 kHz):     |
//                [Dark: + the input an octave down, x X]                  |
//                -> Plate, modulated by the LFO                           |
//                   [Light: an octave up in its loop, x X (shimmer)]      |
//                -> Width -> [Dream: vibrato, x X] -> the mode's low-pass |
//           <- back to 48 kHz -> wet                                      |
//       mix (equal power) and pan <---------------------------------------+
//
// Everything after the gate runs at the core rate, so Stretch lengthens and
// darkens the tail, as the Slöer does by lowering its DSP's sample rate:
// decay times are set for 48 kHz and stretch with it. The LFO keeps its
// rate in Hz.
//
// The gate closes the reverb's input when bypassed (the tail rings out
// with trails on, and is cut in 5 ms with them off) and when Dream's pad is
// frozen. Sustain stretches the decay to 60 s while held; Freeze holds it
// for good. Both ease in and back out, so nothing jumps.
//
// libDaisy-free: main.cpp sets Params once per block and calls Process()
// once per sample.

#pragma once

#include <cstddef>

#include "clock_domain.h"
#include "dsp.h"
#include "lfo.h"
#include "onset_detector.h"
#include "pitch_shift.h"
#include "plate.h"
#include "swell.h"

class SlowEngine
{
  public:
    enum Mode
    {
        kDream,
        kLight,
        kRain,
        kDark,
        kRise,
        kNumModes,
    };

    enum Width
    {
        kNarrow,
        kMedium,
        kWide,
        kNumWidths,
    };

    struct Params
    {
        int   mode    = kDream;
        float decay   = 0.5f; // 0..1: 0.3 s to 20 s
        float mix     = 0.5f; // 0 dry .. 1 wet
        float filter  = 0.6f; // 0 dark .. 1 bright
        float depth   = 0.3f; // modulation, 0..1
        float x       = 0.5f; // the mode's job, 0..1
        float stretch = 0.0f; // 0: 48 kHz .. 1: 12 kHz
        float rate    = 0.4f; // LFO, 0..1: 0.05 Hz to 10 Hz
        float pan     = 0.5f; // 0 wet left, dry right .. 0.5 centred .. 1
        int   shape   = Lfo::kSine;
        int   width   = kWide;
        bool  active  = true;
        bool  trails  = true;
        bool  sustain = false;
        bool  freeze  = false;
    };

    // For the web page and the LEDs.
    struct Probe
    {
        float lfo;       // -1..1
        float input;     // the reverb's input gain, 0..1
        float rt60;      // the decay now, in seconds at the core rate
        float core_rate; // Hz
    };

    static constexpr size_t kVibratoFloats = 1024; // a channel, a power of two

    // The fast memory Init needs, in floats.
    static constexpr size_t kFloats
        = Plate::kFloats + 2 * PitchShift::kFloats + 2 * kVibratoFloats;

    // mem: kFloats floats in fast RAM (RAM_D2). Init clears it.
    void Init(float sample_rate, size_t block_size, float *mem);

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
    void  Tick(float in_l, float in_r, float *out_l, float *out_r);
    float Vibrato(float *line, float x, float delay);

    float sample_rate_ = 48000.0f;
    float block_rate_  = 12000.0f;

    ClockDomain   clock_;
    Plate         plate_;
    Lfo           lfo_, vib_lfo_;
    PitchShift    down_;
    OnsetDetector onset_;
    Swell         swell_;
    dsp::Svf      lp_[2];

    float   *vib_[2]  = {};
    uint32_t vib_pos_ = 0;

    int mode_ = kDream;

    // Smoothing coefficients per block, for the time constants they're named
    // after in SetParams().
    struct
    {
        float fast, slow, rate, diffusion, cutoff, mix, rt_up, rt_down, rt_cut;
    } coef_ = {};

    // Smoothed once per block.
    float rate_ = 48000.0f, log_rt60_ = 0.0f;
    float damping_ = 0.0f, bandwidth_ = 1.0f, diffusion_ = 1.0f;
    float predelay_ = 0.0f, excursion_ = 0.0f;
    float dark_ = 0.0f, shimmer_ = 0.0f, vib_depth_ = 0.0f;
    float out_side_ = 1.0f, in_side_ = 1.0f, width_gain_ = 1.0f;
    float log_cutoff_ = 9.0f, cutoff_set_ = 0.0f, rate_set_ = 0.0f;
    float dry_ = 1.0f, wet_ = 0.0f, mix_ = 0.5f;
    float pan_dry_[2] = {1.0f, 1.0f}, pan_wet_[2] = {1.0f, 1.0f};

    // Per sample: ramps of 5 ms (bypass) and 20 ms (the gate).
    float gate_ = 1.0f, gate_target_ = 1.0f, gate_step_ = 0.0f;
    float bypass_ = 0.0f, bypass_target_ = 0.0f; // 1: the dry at unity
    float cut_ = 0.0f, cut_target_ = 0.0f;       // 1: the wet cut (no trails)
    float fade_step_ = 0.0f;

    float input_     = 1.0f; // the input gain, for the probe
    float wet_peak_  = 0.0f;
};

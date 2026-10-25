// Driftnet - the engine
//
// Two channels that feed each other, after Chase Bliss's MOOD MKII. The
// Wet Channel is a real-time effect (Delay so far); the Micro-Looper loops
// what you just played. Both run in the CLOCK's domain (clock_domain.h); the
// dry signal stays at 48 kHz.
//
// Each core tick:
//   1. the looper plays its loop (silent while listening)
//   2. routing picks what the Wet Channel hears: the input, the input and the
//      loop, or the loop alone (only while the looper plays; otherwise the
//      input)
//   3. the looper records the input plus the Wet Channel while listening, so
//      it catches trails, or overdubs the clean input while overdubbing
//   4. out goes the wet output plus the loop, unless the loop went through
//      the Wet Channel, in which case only its processed version is heard
//
// Mix then sets dry against that, both at full at noon. libDaisy-free.

#pragma once

#include <cstddef>
#include <cstdint>

#include "clock_domain.h"
#include "micro_looper.h"
#include "wet_delay.h"

class DriftEngine
{
  public:
    enum class Routing
    {
        kIn,
        kInLoop,
        kLoop,
    };

    struct Params
    {
        float   time        = 0.5f; // Wet Channel
        float   wet_modify  = 0.4f;
        float   length      = 1.0f; // Micro-Looper
        float   loop_modify = 0.72f;
        float   clock       = 0.9f;
        float   mix         = 0.5f;
        Routing routing     = Routing::kIn;
        bool    wet_on      = false;
        bool    freeze      = false;
    };

    void Init(float  sample_rate,
              float *loop_data,
              size_t loop_frames,
              float *delay_data,
              size_t delay_frames);

    void SetParams(const Params &p); // once per block

    // The looper's footswitch.
    void LooperEngage() { looper_.Engage(); }
    void LooperRelease() { looper_.Release(); }
    void SetOverdub(bool on) { looper_.SetOverdub(on); }

    bool     LooperPlaying() const { return looper_.Playing(); }
    bool     Overdubbing() const { return looper_.Overdubbing(); }
    uint32_t LoopStarts() const { return looper_.LoopStarts(); }
    float    ClockRate() const { return clock_.Rate(); }

    // Both channels off: the app crossfades to the dry input.
    bool Bypassed() const { return !params_.wet_on && !looper_.Playing(); }

    // Once per sample: dry and wet mixed.
    void Process(float in_l, float in_r, float *out_l, float *out_r);

  private:
    void Tick(float in_l, float in_r, float *out_l, float *out_r);

    ClockDomain clock_;
    MicroLooper looper_;
    WetDelay    delay_;
    Params      params_;

    float wet_gain_ = 0.0f, wet_inc_ = 0.0f; // the Wet Channel's on/off fade
    float dry_ = 1.0f, wet_ = 1.0f;          // from Mix
};

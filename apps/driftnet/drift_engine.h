// Driftnet - the engine
//
// Two channels that feed each other, after Chase Bliss's MOOD MKII. The
// Wet Channel is a real-time effect (Reverb, Delay or Slip); the
// Micro-Looper loops what you just played (Env, Tape or Stretch). Both run
// in the CLOCK's domain (clock_domain.h); the dry signal stays at 48 kHz.
// Only the selected Wet Channel mode runs; changing it fades out, resets
// the new mode and fades back in, 10 ms in all.
//
// Each core tick:
//   1. the looper plays its loop (silent while listening)
//   2. routing picks what the Wet Channel hears: the input, the input and the
//      loop, or the loop alone (only while the looper plays; otherwise the
//      input)
//   3. the Wet Channel runs, then TONE's hi-cut and LEVEL BALANCE
//   4. the looper records the input plus the Wet Channel while listening, so
//      it catches trails, or overdubs the clean input while overdubbing
//   5. out goes the wet output plus the loop, unless the loop went through
//      the Wet Channel, in which case only its processed version is heard
//      (DIRECT MICRO-LOOP blends the clean loop back in)
//
// Mix then sets dry against that, both at full at noon. The engine also
// carries MOOD's hidden options and DIP switches (see Params), syncs TIME to
// a tempo or the loop, and lets Synth Mode set the clock and gate the output.
// libDaisy-free.

#pragma once

#include <cstddef>
#include <cstdint>

#include "clock_domain.h"
#include "dsp.h"
#include "micro_looper.h"
#include "wet_delay.h"
#include "wet_reverb.h"
#include "wet_slip.h"

class DriftEngine
{
  public:
    enum class WetMode
    {
        kReverb,
        kDelay,
        kSlip,
    };

    using LoopMode = MicroLooper::Mode;

    enum class Routing
    {
        kIn,
        kInLoop,
        kLoop,
    };

    // SYNC: one channel's timing set by the other's.
    enum class Sync
    {
        kLoopToWet, // the loop is as long as TIME
        kOff,
        kWetToLoop, // TIME steps through fractions of the loop
    };

    // SPREAD SOLO: which channels SPREAD applies to.
    enum class Solo
    {
        kWet,
        kBoth,
        kLoop,
    };

    // Ring buffers: frames * 2 floats each, frames a power of two (the
    // reverb's needn't be). See each mode's Init for how long it needs to be.
    struct Buffers
    {
        float *loop;
        size_t loop_frames;
        float *delay;
        size_t delay_frames;
        float *slip;
        size_t slip_frames;
        float *reverb; // and its diffusers after it: read 16 times a tick, so best in fast RAM
        size_t reverb_frames;
    };

    struct Params
    {
        float    time        = 0.5f; // Wet Channel
        float    wet_modify  = 0.4f;
        float    length      = 1.0f; // Micro-Looper
        float    loop_modify = 0.72f;
        float    clock       = 0.9f;
        float    mix         = 0.5f;
        WetMode  wet_mode    = WetMode::kDelay;
        LoopMode loop_mode   = LoopMode::kTape;
        Routing  routing     = Routing::kIn;
        bool     wet_on      = false;
        bool     freeze      = false;

        // Hidden options
        float tone        = 1.0f; // TONE: hi-cut on the Wet Channel, 1 = open
        float width       = 1.0f; // STEREO WIDTH: SPREAD on the Wet Channel
        float direct      = 0.0f; // DIRECT MICRO-LOOP: clean loop blended back
        float fade        = 1.0f; // FADE: what each overdub pass keeps, 1 = all
        float balance     = 0.5f; // LEVEL BALANCE: above noon turns the wet down
        Sync  sync        = Sync::kOff;
        Solo  solo        = Solo::kBoth;
        bool  half_length = false; // LOOP LENGTH half

        // DIP switches (MISO and LATCH are the app's)
        bool classic  = false;
        bool spread   = false;
        bool dry_kill = false;
        bool trails   = false;
        bool no_dub   = false;
        bool smooth   = false;

        // > 0: TIME (and Tape's LENGTH) follow this tempo, in samples per
        // beat at the codec rate.
        float samples_per_beat = 0.0f;

        // Synth Mode: > 0 sets the clock; gate scales the wet and the loop.
        float clock_hz = 0.0f;
        float gate     = 1.0f;
    };

    void Init(float sample_rate, const Buffers &buffers);

    void SetParams(const Params &p); // once per 4-sample block

    // The looper's footswitch.
    void LooperEngage() { looper_.Engage(); }
    void LooperRelease() { looper_.Release(); }
    void SetOverdub(bool on) { looper_.SetOverdub(on); }

    bool     LooperPlaying() const { return looper_.Playing(); }
    bool     Overdubbing() const { return looper_.Overdubbing(); }
    uint32_t LoopStarts() const { return looper_.LoopStarts(); }
    float    ClockRate() const { return clock_.Rate(); }
    float    Envelope() const { return looper_.Envelope(); } // Env's detector
    float    Threshold() const { return looper_.Threshold(); }

    // Both channels off and no trails ringing: the app crossfades to the
    // dry input.
    bool Bypassed() const { return Idle() && !params_.trails; }

    // For the web page.
    void Snapshot(MicroLooper::View *v) const { looper_.Snapshot(v); }
    // The wet and loop peaks since the last call.
    void TakePeaks(float *wet, float *loop);

    // Once per sample: dry and wet mixed.
    void Process(float in_l, float in_r, float *out_l, float *out_r);

  private:
    bool Idle() const { return !params_.wet_on && !looper_.Playing(); }

    void  Tick(float in_l, float in_r, float *out_l, float *out_r);
    void  ProcessWet(float in_l, float in_r, float *out_l, float *out_r);
    float SyncedTime(float rate) const; // frames, or 0 when TIME isn't synced

    ClockDomain clock_;
    MicroLooper looper_;
    WetReverb   reverb_;
    WetDelay    delay_;
    WetSlip     slip_;
    Params      params_;
    float       sample_rate_ = 48000.0f;

    WetMode wet_mode_  = WetMode::kDelay; // running; params_.wet_mode is wanted
    float   mode_gain_ = 1.0f;            // the mode change fade

    // The Wet Channel on and off: what it hears, and what's heard of it
    // (which TRAILS keeps up after it's switched off).
    float send_gain_ = 0.0f, out_gain_ = 0.0f, wet_inc_ = 0.0f;

    float smooth_hz_ = 48000.0f, smooth_coef_ = 0.0f; // SMOOTH's glide

    dsp::Biquad tone_[2];
    float       tone_set_ = -1.0f, tone_rate_ = 0.0f; // what tone_ was set for
    bool        tone_on_  = false;

    float wet_bal_ = 1.0f, loop_bal_ = 1.0f; // LEVEL BALANCE
    float dry_ = 1.0f, wet_ = 1.0f;          // from Mix
    float dry_now_ = 1.0f, dry_inc_ = 0.0f;  // unity while idle
    float gate_now_ = 1.0f;
    float noise_ = 0.0f; // CLASSIC: hiss as the clock comes down
    dsp::Rng rng_;

    float peak_wet_ = 0.0f, peak_loop_ = 0.0f;
};

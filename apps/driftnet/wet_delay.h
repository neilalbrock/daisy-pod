// Driftnet - Delay mode
//
// A clean delay, timed in core frames so the CLOCK stretches it. Changing
// TIME doesn't bend the pitch of what's already echoing: a second read head
// jumps to the new time and the two crossfade. MODIFY is feedback; at max it
// is exactly 1, so repeats hold like a looper. Freeze stops the input and
// holds the feedback at 1.
//
// SPREAD crosses the feedback over by WIDTH, so echoes ping-pong, mirroring
// where the input was panned. CLASSIC glides a single read head to a new
// TIME instead, bending the pitch of the echoes like MOOD MKI. Runs once
// per core tick. libDaisy-free.

#pragma once

#include <cstddef>
#include <cstdint>

#include "sample_buffer.h"

class WetDelay
{
  public:
    // data: frames * 2 floats. frames must be a power of two.
    void Init(float *data, size_t frames);

    // Once per block.
    void SetRate(float core_rate); // for the crossfade time
    void SetTime(float time);      // 0..1
    void SetTimeFrames(float frames); // synced: a time in frames instead
    void SetFeedback(float modify);
    void SetFreeze(bool on) { freeze_ = on; }
    void SetSpread(float width) { spread_ = width; } // 0: off
    void SetClassic(bool on) { classic_ = on; }

    // TIME as frames, for SYNC to set the loop from.
    float TimeFrames(float time) const;
    float MaxFrames() const { return (float)max_delay_; }

    // Forget what's in the ring, without clearing it: switching back to
    // Delay doesn't bring old echoes back.
    void Reset() { fresh_ = 0; }

    void Process(float in_l, float in_r, float *out_l, float *out_r);

  private:
    void Head(uint32_t delay, float *l, float *r) const; // 0 if not yet written
    void HeadAt(float delay, float *l, float *r) const;  // interpolated

    SampleBuffer buffer_;
    uint32_t     fresh_     = 0; // frames written since Reset()
    uint32_t     max_delay_ = 0;
    uint32_t     target_    = 0;
    uint32_t     delay_[2]  = {}; // read heads, frames behind the write head
    int          active_    = 0;  // the head being heard, unless fading
    bool         fading_    = false;
    float        fade_      = 0.0f; // 0 = head 0, 1 = head 1
    float        fade_inc_  = 0.0f;
    float        feedback_  = 0.0f;
    bool         freeze_    = false;
    float        spread_    = 0.0f;
    bool         classic_   = false;
    float        bend_ = 0.0f, bend_coef_ = 0.0f; // CLASSIC's gliding head, frames
    float        rate_set_ = -1.0f, time_set_ = -1.0f; // the setters skip repeats
};

// Driftnet - the Micro-Looper
//
// An always-listening looper: while bypassed it records into a ring without
// stopping, and engaging it loops the last loop's worth of the ring, so you
// catch whatever was just played. There is no loop length to set: the ring
// holds a fixed number of core frames, so the CLOCK sets how many seconds
// that is. Bypassing it again starts recording over the loop, oldest first.
//
// Tape mode plays the loop at 0.5x, 1x, 2x or 4x, forward or reversed, and
// LENGTH shrinks the part that loops, down to 1/32, keeping the newest end
// of it. Overdubs are written at the play head, so at other speeds they land
// wherever it is.
//
// The ring keeps a short stretch of audio before the loop's start; at the
// end of each pass the play head crossfades into it, so the seam doesn't
// click. All of this runs once per core tick. libDaisy-free.

#pragma once

#include <cstddef>
#include <cstdint>

#include "sample_buffer.h"

class MicroLooper
{
  public:
    // data: frames * 2 floats. frames must be a power of two.
    void Init(float *data, size_t frames);

    // Once per block: the core rate (for fade times), LENGTH and MODIFY.
    void SetRate(float core_rate);
    void SetLength(float length);
    void SetModify(float modify);

    void Engage();  // loop the last loop's worth of the ring
    void Release(); // back to listening
    void SetOverdub(bool on);

    bool     Playing() const { return playing_; }
    bool     Overdubbing() const { return playing_ && dub_target_ > 0.0f; }
    float    Gain() const { return gain_; } // fades in on Engage, out on Release
    uint32_t LoopStarts() const { return loop_starts_; }

    // Each core tick: Read() the loop, then Write() what it should hear. While
    // listening Write() records; while playing it overdubs.
    void Read(float *l, float *r);
    void Write(float l, float r);

  private:
    static constexpr uint32_t kSeam = 512; // crossfade at the loop's end, frames

    SampleBuffer buffer_;
    float        loop_len_ = 0.0f; // frames: the ring less the seam
    float        window_   = 0.0f; // the part that loops, at its end, frames
    uint32_t     start_    = 0;    // the loop's first frame in the ring
    float        pos_      = 0.0f; // play head, frames into the window
    float        speed_    = 1.0f;
    bool         playing_  = false;

    float gain_ = 0.0f, gain_inc_ = 0.0f;
    float dub_ = 0.0f, dub_target_ = 0.0f;
    uint32_t dub_frame_   = 0; // where the play head was this tick
    uint32_t last_dubbed_ = 0xFFFFFFFFu;

    uint32_t loop_starts_ = 0;
};

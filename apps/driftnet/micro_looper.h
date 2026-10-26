// Driftnet - the Micro-Looper
//
// An always-listening looper: while bypassed it records into a ring without
// stopping, and engaging it loops the last loop's worth of the ring, so you
// catch whatever was just played. There is no loop length to set: the ring
// holds a fixed number of core frames, so the CLOCK sets how many seconds
// that is. Bypassing it again starts recording over the loop, oldest first.
//
// Three modes play the loop, and switching between them keeps it:
//   Env     - plays at 1x, cut into slices of LENGTH. While your input is
//             louder than MODIFY's threshold it repeats the current slice.
//   Tape    - plays at 0.5x, 1x, 2x or 4x, forward or reversed, and LENGTH
//             shrinks the part that loops, down to 1/32, keeping the newest
//             end of it.
//   Stretch - two Hann-windowed grains of LENGTH play at 1x while the place
//             they read from moves at MODIFY's rate: frozen at noon, faster
//             towards either end, until at the ends it plays straight.
// Overdubs are written at the play head, so in all but Tape at 1x they land
// wherever it happens to be.
//
// The ring keeps a short stretch of audio before the loop's start; at the
// end of each pass the play head crossfades into it, so the seam doesn't
// click (and an Env slice does the same into the audio before it). All of
// this runs once per core tick. libDaisy-free.

#pragma once

#include <cstddef>
#include <cstdint>

#include "sample_buffer.h"

class MicroLooper
{
  public:
    enum class Mode
    {
        kEnv,
        kTape,
        kStretch,
    };

    // data: frames * 2 floats. frames must be a power of two.
    void Init(float *data, size_t frames);

    // Once per block: the core rate (for fade and envelope times), the mode,
    // LENGTH and MODIFY. A mode change fades out and back in.
    void SetRate(float core_rate);
    void SetMode(Mode mode) { next_mode_ = mode; }
    void SetLength(float length);
    void SetModify(float modify);

    void Engage();  // loop the last loop's worth of the ring
    void Release(); // back to listening
    void SetOverdub(bool on);

    bool     Playing() const { return playing_; }
    bool     Overdubbing() const { return playing_ && dub_target_ > 0.0f; }
    float    Gain() const { return gain_; } // fades in on Engage, out on Release
    uint32_t LoopStarts() const { return loop_starts_; }

    // Each core tick: Sense() the input (Env listens to it), Read() the
    // loop, then Write() what it should hear. While listening Write()
    // records; while playing it overdubs.
    void Sense(float l, float r);
    void Read(float *l, float *r);
    void Write(float l, float r);

  private:
    static constexpr uint32_t kSeam = 512; // crossfade at the loop's end, frames

    struct Grain
    {
        float start; // loop position it started reading from
        float age, size;
        float dir; // 1 forward, -1 reversed
    };

    // Reads `len` frames from `first` at q (0..len), fading into the frames
    // before `first` near the end, so wrapping q to 0 doesn't click. Returns
    // the frame under q, where an overdub would go.
    uint32_t ReadSeamless(uint32_t first, float len, float q, float *l, float *r) const;

    void ReadTape(float *l, float *r);
    void ReadEnv(float *l, float *r);
    void ReadStretch(float *l, float *r);

    void  Begin(); // place the play head for the mode, at the loop's start
    void  SwitchMode(Mode mode);
    float LoopPos() const; // the play head, frames into the loop
    void  SetLoopPos(float pos);
    void  StartGrains();

    SampleBuffer buffer_;
    float        loop_len_ = 0.0f; // frames: the ring less the seam
    uint32_t     start_    = 0;    // the loop's first frame in the ring
    bool         playing_  = false;
    Mode         mode_ = Mode::kTape, next_mode_ = Mode::kTape;
    float        length_ = 1.0f, modify_ = 0.72f;

    // Tape: the play head, frames into the window that loops, which is the
    // newest part of the loop.
    float pos_ = 0.0f, window_ = 0.0f, speed_ = 1.0f;

    // Env (its play head is pos_, frames into the loop).
    float env_ = 0.0f, env_release_ = 0.0f, threshold_ = 0.0f;
    float slice_len_ = 0.0f;           // from LENGTH
    float slice_ = 0.0f, repeat_len_ = 0.0f; // the slice being repeated
    bool  repeating_ = false;

    // Stretch (its read position is pos_, frames into the loop).
    Grain grains_[2];
    float grain_size_ = 0.0f, rate_ = 1.0f, dir_ = 1.0f;

    float gain_ = 0.0f, gain_inc_ = 0.0f;
    float mode_gain_ = 1.0f;
    float dub_ = 0.0f, dub_target_ = 0.0f;
    uint32_t dub_frame_   = 0; // where the play head was this tick
    uint32_t last_dubbed_ = 0xFFFFFFFFu;

    uint32_t loop_starts_ = 0;
};

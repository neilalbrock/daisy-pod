// Driftnet - the Micro-Looper
//
// An always-listening looper: while bypassed it records into a ring without
// stopping, and engaging it loops the last loop's worth of the ring, so you
// catch whatever was just played. There is no loop length to set: the ring
// holds a fixed number of core frames, so the CLOCK sets how many seconds
// that is (LOOP LENGTH can halve it, and SYNC can set it from TIME).
// Bypassing it again starts recording over the loop, oldest first.
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
// wherever it happens to be. FADE lets each overdub pass fade the loop, and
// NO DUB replaces it outright.
//
// SPREAD gives each mode a stereo movement: Env pans while it repeats, Tape
// plays the loop reversed on the left, and Stretch pans slowly. CLASSIC
// wears the loop a little on every pass, and makes Tape's LENGTH chop the
// loop instead of shortening it.
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

    struct Options
    {
        float    fade    = 1.0f;  // FADE: how much of the loop an overdub pass keeps
        bool     no_dub  = false; // NO DUB: overdubs replace the loop
        bool     classic = false; // CLASSIC: wear, Tape chops, NO DUB mutes the loop
        bool     half    = false; // LOOP LENGTH: half the ring, like MOOD MKI
        bool     synced  = false; // Tape's LENGTH in whole fractions of the loop
        bool     spread  = false; // SPREAD
        uint32_t capture = 0;     // SYNC: frames to loop on Engage (0: all of them)
    };

    // One read head, for the web page.
    struct Head
    {
        uint32_t start;  // first frame of what it loops
        float    length; // frames
        uint32_t pos;    // frame being read
        float    rate;
        bool     reverse;
        float    amp;
    };

    struct View
    {
        bool     playing;
        Mode     mode;
        uint32_t write; // the ring's write head
        uint32_t start; // the loop's first frame
        float    loop_len;
        uint32_t pos; // the play head
        int      num_heads;
        Head     heads[2]; // Tape's window, the Env slice (or loop), Stretch's grains
        float    env, threshold; // Env's detector
    };

    // data: frames * 2 floats. frames must be a power of two.
    void Init(float *data, size_t frames);

    // Once per block: the core rate (for fade and envelope times), the mode,
    // the options, then LENGTH and MODIFY. A mode change fades out and back
    // in.
    void SetRate(float core_rate);
    void SetMode(Mode mode) { next_mode_ = mode; }
    void SetOptions(const Options &options) { options_ = options; }
    void SetLength(float length);
    void SetModify(float modify);

    void Engage();  // loop the last loop's worth of the ring
    void Release(); // back to listening
    void SetOverdub(bool on);

    bool     Playing() const { return playing_; }
    bool     Overdubbing() const { return playing_ && dub_target_ > 0.0f; }
    float    Gain() const { return gain_; } // fades in on Engage, out on Release
    uint32_t LoopStarts() const { return loop_starts_; }
    // The length that plays: Tape's window, or the whole loop.
    float PlayedLength() const { return mode_ == Mode::kTape ? TapeWindow() : loop_len_; }
    float RingLength() const { return (float)(buffer_.Frames() - kSeam); }
    float Envelope() const { return env_; } // Env's detector and threshold
    float Threshold() const { return threshold_; }

    void Snapshot(View *v) const;

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
    void Pan(float *l, float *r, float amount) const; // SPREAD's mono pan

    // What Tape loops: the window, or all of it under CLASSIC (which chops).
    float TapeWindow() const { return options_.classic ? loop_len_ : window_; }

    void  Begin(); // place the play head for the mode, at the loop's start
    void  SwitchMode(Mode mode);
    float LoopPos() const; // the play head, frames into the loop
    void  SetLoopPos(float pos);
    void  StartGrains();

    SampleBuffer buffer_;
    Options      options_;
    float        loop_len_ = 0.0f; // frames, without the seam
    uint32_t     start_    = 0;    // the loop's first frame in the ring
    bool         playing_  = false;
    Mode         mode_ = Mode::kTape, next_mode_ = Mode::kTape;
    float        length_ = 1.0f, modify_ = 0.72f;
    float        chop_coef_ = 0.0f; // CLASSIC's gate in Tape

    // What the setters last worked from, so they only redo the maths on a
    // change.
    float rate_set_ = -1.0f, length_set_ = -1.0f, loop_len_set_ = -1.0f, modify_set_ = -1.0f;
    bool  synced_set_ = false;

    // Tape: the play head, frames into the window that loops, which is the
    // newest part of the loop.
    float pos_ = 0.0f, window_ = 0.0f, speed_ = 1.0f;
    float chop_ = 1.0f; // CLASSIC's gate

    // Env (its play head is pos_, frames into the loop).
    float env_ = 0.0f, env_release_ = 0.0f, threshold_ = 0.0f;
    float slice_len_ = 0.0f;                 // from LENGTH
    float slice_ = 0.0f, repeat_len_ = 0.0f; // the slice being repeated
    bool  repeating_ = false;

    // Stretch (its read position is pos_, frames into the loop).
    Grain grains_[2];
    float grain_size_ = 0.0f, rate_ = 1.0f, dir_ = 1.0f;

    // SPREAD's pan (Env and Stretch).
    float pan_phase_ = 0.0f, pan_amount_ = 0.0f;

    float gain_ = 0.0f, gain_inc_ = 0.0f;
    float mode_gain_ = 1.0f;
    float dub_ = 0.0f, dub_target_ = 0.0f;
    uint32_t dub_frame_   = 0; // where the play head was this tick
    uint32_t last_dubbed_ = 0xFFFFFFFFu;
    float    wear_[2]     = {}; // CLASSIC: the last frame worn, per side

    uint32_t loop_starts_ = 0;
};

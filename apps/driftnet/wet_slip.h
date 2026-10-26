// Driftnet - Slip mode
//
// An auto-sampler. A ring records the input without stopping, and two
// heads take turns playing back its newest stretch at another speed, each
// faded in and out with a Hann window, half a window apart. TIME sets the
// window: short ones act as a pitch shifter, long ones play phrases that
// follow behind you. MODIFY sets the speed in semitone steps: 1x forward at
// noon (a plain echo, one window late), up to 2x forward clockwise, and
// from 1x to 2x reversed anticlockwise. Each head keeps the window and
// speed it started with, so turning MODIFY steps the harmony one window at
// a time. Freeze stops the recording, so the heads keep replaying the same
// window.
//
// SPREAD pans the output from side to side, once every two windows, as far
// as WIDTH lets it. CLASSIC reads without interpolation and with abrupt
// window edges, for MOOD MKI's crunchy artefacts.
//
// Timed in core frames, so the CLOCK scales it, and runs once per core tick.
// libDaisy-free.

#pragma once

#include <cstddef>
#include <cstdint>

#include "sample_buffer.h"

class WetSlip
{
  public:
    // data: frames * 2 floats. frames must be a power of two, over three
    // times the longest window (96000 frames): a head reversed at 2x ends
    // up that far back.
    void Init(float *data, size_t frames);

    // Once per block.
    void SetTime(float time);     // 0..1
    void SetWindowFrames(float frames); // synced: a window in frames instead
    void SetModify(float modify); // 0..1
    void SetFreeze(bool on) { freeze_ = on; }
    void SetSpread(float width) { spread_ = width; } // 0: off
    void SetClassic(bool on) { classic_ = on; }

    // Forget what's in the ring, without clearing it: switching back to
    // Slip doesn't replay old audio.
    void Reset();

    void Process(float in_l, float in_r, float *out_l, float *out_r);

  private:
    struct Head
    {
        uint32_t anchor; // the newest frame when the head started
        float    offset; // where it starts reading, from the anchor
        float    speed;  // negative: reversed
        float    life;   // frames, one window
        float    age;
        bool     live; // all it will read was recorded; otherwise silent
    };

    void Start(Head &h, float age);

    SampleBuffer buffer_;
    uint32_t     fresh_ = 0; // frames recorded since Reset()
    Head         heads_[2];
    float        window_ = 0.0f;
    int          zone_   = 13; // MODIFY's semitone zone, 13 = 1x forward
    float        speed_  = 1.0f;
    bool         freeze_ = false;
    float        spread_ = 0.0f, pan_phase_ = 0.0f;
    bool         classic_ = false;
    float        time_set_ = -1.0f, modify_set_ = -1.0f; // the setters skip repeats
};

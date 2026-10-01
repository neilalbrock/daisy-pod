// Grainfall - MIDI clock follower
//
// Fed with MIDI clock ticks (24 per quarter note), Start messages and a
// running sample count, it tracks the tempo and gives a continuous position
// in ticks that the audio code can quantise against. Call it only from the
// audio callback.

#pragma once

#include <cstddef>
#include <cstdint>

class MidiClock
{
  public:
    static constexpr int kTicksPerBeat = 24;
    // Position wraps every 4 bars so float precision never degrades. That's a
    // whole number of steps of every synced division, so grids stay aligned.
    static constexpr int kWrapTicks = kTicksPerBeat * 16;

    void Init(float sample_rate);

    // `now` is the running sample count at which the message was seen.
    void Tick(uint32_t now);
    void Start(); // next tick is the downbeat (tick 0)

    // True while ticks are arriving steadily.
    bool Locked(uint32_t now) const;

    float SamplesPerTick() const { return samples_per_tick_; }

    // Position in ticks since Start (modulo kWrapTicks), between received
    // ticks interpolated from the tempo. Only meaningful while Locked().
    float Position(uint32_t now) const;

    // True between a Start message and the tick that marks the downbeat.
    bool AwaitingDownbeat() const { return tick_count_ < 0; }

  private:
    static constexpr size_t kHistory = kTicksPerBeat; // average over one beat

    float    sample_rate_      = 48000.0f;
    uint32_t timeout_          = 0; // samples without a tick before unlocking
    uint32_t last_tick_        = 0;
    bool     have_last_        = false;
    int32_t  tick_count_       = 0;
    float    samples_per_tick_ = 0.0f;

    uint32_t intervals_[kHistory] = {};
    size_t   num_intervals_       = 0;
    size_t   next_interval_       = 0;
    uint32_t interval_sum_        = 0;
};

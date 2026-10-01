#include "midi_clock.h"

void MidiClock::Init(float sample_rate)
{
    sample_rate_ = sample_rate;
    // A tick at 20 BPM is 125 ms apart; anything much slower means the clock
    // has stopped.
    timeout_       = (uint32_t)(0.3f * sample_rate_);
    have_last_     = false;
    tick_count_    = 0;
    num_intervals_ = 0;
    next_interval_ = 0;
    interval_sum_  = 0;
}

void MidiClock::Tick(uint32_t now)
{
    if(have_last_)
    {
        uint32_t interval = now - last_tick_; // unsigned: survives wraparound
        if(interval > timeout_)
        {
            // Clock resumed after a gap: the old tempo is stale.
            num_intervals_ = 0;
            next_interval_ = 0;
            interval_sum_  = 0;
        }
        else
        {
            if(num_intervals_ == kHistory)
                interval_sum_ -= intervals_[next_interval_];
            else
                num_intervals_++;
            intervals_[next_interval_] = interval;
            interval_sum_ += interval;
            next_interval_    = (next_interval_ + 1) % kHistory;
            samples_per_tick_ = (float)interval_sum_ / (float)num_intervals_;
        }
    }
    last_tick_ = now;
    have_last_ = true;
    if(++tick_count_ >= kWrapTicks)
        tick_count_ = 0;
}

void MidiClock::Start()
{
    tick_count_ = -1;
}

bool MidiClock::Locked(uint32_t now) const
{
    // A few intervals are enough for a usable tempo; it settles over a beat.
    return have_last_ && num_intervals_ >= 4 && now - last_tick_ <= timeout_;
}

float MidiClock::Position(uint32_t now) const
{
    float since = (float)(now - last_tick_) / samples_per_tick_;
    // Never run past the next tick that hasn't arrived yet, so the position
    // can't jump backwards when it does.
    if(since > 0.999f)
        since = 0.999f;
    float pos = (float)tick_count_ + since;
    // Between Start and its first tick, hold at the downbeat.
    return pos < 0.0f ? 0.0f : pos;
}

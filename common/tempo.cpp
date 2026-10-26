#include "tempo.h"

#include <cmath>

namespace
{
constexpr float kMinBeatS = 0.2f; // 300 BPM
constexpr float kMaxBeatS = 2.0f; // 30 BPM; a longer gap starts a new count
} // namespace

void Tempo::Init(float sample_rate)
{
    sample_rate_      = sample_rate;
    samples_per_beat_ = 0.5f * sample_rate;
    clock_.Init(sample_rate);
}

void Tempo::MidiStart()
{
    clock_.Start();
    external_ = true;
}

void Tempo::Tap(uint32_t now)
{
    if(following_)
        return;

    uint32_t interval = now - last_tap_;
    if(have_tap_ && interval >= kMinBeatS * sample_rate_
       && interval <= kMaxBeatS * sample_rate_)
    {
        taps_[next_tap_] = interval;
        next_tap_        = (next_tap_ + 1) % kTapHistory;
        if(num_taps_ < kTapHistory)
            num_taps_++;
        uint32_t sum = 0;
        for(int i = 0; i < num_taps_; i++)
            sum += taps_[i];
        samples_per_beat_ = (float)sum / (float)num_taps_;
        has_tempo_        = true;
    }
    else
    {
        num_taps_ = 0;
        next_tap_ = 0;
    }
    last_tap_ = now;
    have_tap_ = true;

    // Put the beat on the tap.
    beats_ = floorf(beats_ + 0.5f);
    if(beats_ >= (float)kWrapBeats)
        beats_ -= (float)kWrapBeats;
}

void Tempo::SetBpm(float bpm)
{
    if(!following_ && bpm > 0.0f)
    {
        samples_per_beat_ = 60.0f * sample_rate_ / bpm;
        has_tempo_        = true;
    }
}

void Tempo::Update(uint32_t now)
{
    following_ = external_ && clock_.Locked(now);
    if(following_)
    {
        beats_ = clock_.Position(now) / (float)MidiClock::kTicksPerBeat;
        samples_per_beat_
            = clock_.SamplesPerTick() * (float)MidiClock::kTicksPerBeat;
    }
    else
    {
        beats_ += (float)(now - last_now_) / samples_per_beat_;
        while(beats_ >= (float)kWrapBeats)
            beats_ -= (float)kWrapBeats;
    }
    last_now_ = now;
}

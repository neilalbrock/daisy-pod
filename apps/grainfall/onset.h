// Grainfall - note onsets
//
// OnsetDetector (common/onset_detector.h) flags the start of a note.
// OnsetLog keeps where in the sample buffer the last few onsets landed, for
// the effects that play back recent notes (Strum, Arp, Blocks...). Header
// only, libDaisy-free.

#pragma once

#include <cstdint>

#include "onset_detector.h"

class OnsetLog
{
  public:
    static constexpr int kSize = 8;

    void Add(uint32_t frame)
    {
        next_         = (next_ + 1) % kSize;
        frames_[next_] = frame;
        if(count_ < kSize)
            count_++;
    }

    void Clear() { count_ = 0; }
    int  Count() const { return count_; }

    // 0 is the newest onset, Count() - 1 the oldest.
    uint32_t Get(int i) const { return frames_[(next_ - i + kSize) % kSize]; }

  private:
    uint32_t frames_[kSize] = {};
    int      next_          = 0;
    int      count_         = 0;
};

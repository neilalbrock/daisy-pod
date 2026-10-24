// Tap or hold on one button
//
// Tells a short press from a long hold on the same button. A hold reports as
// soon as the button has been down long enough, so it acts while still held;
// a press let go before then reports as a tap on release. The app reads the
// button and passes the state in, so this doesn't depend on libDaisy. Call
// Update() once per block.

#pragma once

#include <cstdint>

class PressHold
{
  public:
    enum class Event
    {
        kNone,
        kTap,  // released before the hold time
        kHold, // held for the hold time; no tap follows on release
    };

    explicit PressHold(uint32_t hold_ms) : hold_ms_(hold_ms) {}

    // pressed: the button is down now; held_ms: how long it has been down.
    Event Update(bool pressed, uint32_t held_ms)
    {
        if(pressed)
        {
            down_ = true;
            if(!held_ && held_ms >= hold_ms_)
            {
                held_ = true;
                return Event::kHold;
            }
            return Event::kNone;
        }
        Event e = down_ && !held_ ? Event::kTap : Event::kNone;
        down_   = false;
        held_   = false;
        return e;
    }

  private:
    uint32_t hold_ms_;
    bool     down_ = false;
    bool     held_ = false; // this press has already reported a hold
};

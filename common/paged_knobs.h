// Paged knobs with soft takeover
//
// Shares a few physical knobs across pages of parameters. After a page
// change (or at startup) a knob does nothing until it moves, so values don't
// jump to wherever the knob happens to be. The app reads the knobs and
// passes the values in, so this doesn't depend on libDaisy. Call it only
// from the audio callback, apart from Latch() before audio starts.

#pragma once

#include <cmath>

template <int Pages, int Knobs = 2>
class PagedKnobs
{
  public:
    // initial: starting values (0..1), held until each knob first moves.
    explicit PagedKnobs(const float (&initial)[Pages][Knobs])
    {
        for(int p = 0; p < Pages; p++)
            for(int k = 0; k < Knobs; k++)
                values_[p][k] = initial[p][k];
    }

    // Records where the knobs are now; each takes over only once it moves
    // away from here.
    void Latch(const float (&knobs)[Knobs])
    {
        for(int k = 0; k < Knobs; k++)
        {
            at_entry_[k] = knobs[k];
            live_[k]     = false;
        }
    }

    // Moves to the next page (wrapping) and latches the knobs there.
    void NextPage(const float (&knobs)[Knobs])
    {
        page_ = (page_ + 1) % Pages;
        Latch(knobs);
    }

    // Once per block: a knob that has moved writes into the current page.
    void Update(const float (&knobs)[Knobs])
    {
        for(int k = 0; k < Knobs; k++)
        {
            if(!live_[k] && fabsf(knobs[k] - at_entry_[k]) > kTakeover)
                live_[k] = true;
            if(live_[k])
                values_[page_][k] = knobs[k];
        }
    }

    int   Page() const { return page_; }
    float Value(int page, int knob) const { return values_[page][knob]; }

  private:
    static constexpr float kTakeover = 0.02f; // movement that hands over control

    float values_[Pages][Knobs];
    float at_entry_[Knobs] = {};
    bool  live_[Knobs]     = {};
    int   page_            = 0;
};

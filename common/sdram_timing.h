// SDRAM row timings for the Seed's AS4C16M32SB-6
//
// libDaisy waits 16 SDRAM clocks to precharge a row (TRP) and 10 to
// activate one (TRCD). The part needs 18 ns for each, and libDaisy runs
// SDCLK at 100 MHz (10 ns). That matters because every voice, tap and write
// head in an app works in a different row of the same internal bank (16 MB
// each), so nearly every cache-line fill opens a new row. Cutting both to
// 3 clocks (30 ns) took about 40% off SDRAM-heavy work on the bench, and
// grainfall's worst case (Warp D) from 51% CPU to 39% (peak 58% to 45%).
//
// TRAS also goes from libDaisy's 4 clocks to 5: the part needs 42 ns.
//
// Call once, straight after hw.Init(). Unlike the rest of common/, this
// touches the hardware, so only main.cpp should include it.

#pragma once

#include "daisy_seed.h"

inline void TuneSdramTimings()
{
    // SDTR1 fields hold clocks - 1: TRAS bits 11:8, TRP 23:20, TRCD 27:24.
    constexpr uint32_t kMask = (15u << 8) | (15u << 20) | (15u << 24);
    constexpr uint32_t kSet  = ((5u - 1) << 8) | ((3u - 1) << 20) | ((3u - 1) << 24);

    FMC_Bank5_6_R->SDTR[0] = (FMC_Bank5_6_R->SDTR[0] & ~kMask) | kSet;
    __DSB();
}

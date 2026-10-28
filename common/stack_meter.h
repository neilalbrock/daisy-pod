// How much of the stack has never been used
//
// The stack sits at the top of DTCM, below the app's globals, and the main
// loop and every interrupt (audio, USB, MIDI) share it. Nothing stops it
// growing down into the globals, so an app near the top of DTCM wants to
// know its margin. Paint() fills the free space below the stack with a
// pattern, first thing in main(); Free() counts how much of it is still
// untouched: the least there has been to spare since power-up.
//
// Free() reads up from the bottom until the pattern ends (a few KB), so
// call it from the main loop, not the audio callback. Like qspi_flash.h,
// this touches the hardware, so only main.cpp should include it.

#pragma once

#include <cstddef>
#include <cstdint>

#include "daisy_seed.h"

// From libDaisy's linker scripts: the end of the last section in DTCM.
extern "C" uint32_t __reserved_for_stack_end__[];

namespace stack_meter
{
constexpr uint32_t kPaint = 0x5ca1ab1eu;

inline uint32_t *Bottom()
{
    return __reserved_for_stack_end__;
}

// Before anything else in main(): fills from the bottom of the free space
// to a little below where the stack is now.
inline void Paint()
{
    uint32_t *top = reinterpret_cast<uint32_t *>(__get_MSP()) - 64;
    for(volatile uint32_t *p = Bottom(); p < top; p++)
        *p = kPaint;
}

// Bytes never used since Paint().
inline size_t Free()
{
    const uint32_t *p   = Bottom();
    const uint32_t *top = reinterpret_cast<const uint32_t *>(__get_MSP());
    while(p < top && *p == kPaint)
        p++;
    return (size_t)(p - Bottom()) * sizeof(uint32_t);
}
} // namespace stack_meter

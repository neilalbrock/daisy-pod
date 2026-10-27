// Wavebyte - wavetables
//
// 64 wavetables in the Microwave's layout: each holds 64 waves of 128
// 8-bit samples. Waves 0-60 are the table's own, a sweep from one sound to
// another; 61, 62 and 63 are the same in every table: triangle, square and
// saw, as on the Protein. The waves are our own, generated at power-up
// (about 512 KB, in SDRAM): families of sweeps (resonance, hard sync, pulse
// width, formants, clipping, FM, bells, phase distortion, bit reduction,
// random spectra...) named after what they do.
//
// Each wave is normalised to full scale with its DC removed, then rounded
// to 8 bits, the Microwave's sample size.
//
// libDaisy-free.

#pragma once

#include <cstdint>

class Wavetables
{
  public:
    static constexpr int kTables     = 64;
    static constexpr int kWaves      = 64;
    static constexpr int kTableWaves = 61; // the table's own; then triangle, square, saw
    static constexpr int kLength     = 128;

    using Wave = int8_t[kLength];

    // Fills `memory` (kTables x kWaves waves; put it in SDRAM). Takes a
    // fraction of a second.
    void Init(Wave *memory);

    const int8_t *Get(int table, int wave) const { return waves_[table * kWaves + wave]; }

    static const char *const kNames[kTables];

  private:
    Wave *waves_ = nullptr;
};

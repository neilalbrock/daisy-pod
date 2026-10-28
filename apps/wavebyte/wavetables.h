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
// Then a second bank, tables 64-127, from the AKWF single cycles (CC0;
// akwf_bank.h, written by tools/akwf_tables.py): each built at boot from
// key waves crossfaded across the table, or from one source wave through a
// family (sync, fold, PWM, squeeze), worked out at 512 points and
// band-limited to the 63 harmonics a wave holds.
//
// libDaisy-free.

#pragma once

#include <cstdint>

#include "akwf_bank.h"

class Wavetables
{
  public:
    static constexpr int kOwnTables  = 64; // ours, then the AKWF bank
    static constexpr int kTables     = kOwnTables + akwf::kTables;
    static constexpr int kWaves      = 64;
    static constexpr int kTableWaves = 61; // the table's own; then triangle, square, saw
    static constexpr int kLength     = 128;

    using Wave = int8_t[kLength];

    static constexpr int kScratchFloats = 3 * 512; // for the AKWF families

    // Fills `memory` (kTables x kWaves waves; put it in SDRAM), working in
    // `scratch` (kScratchFloats; SDRAM will do, and keeps it off the stack).
    // Takes a fraction of a second.
    void Init(Wave *memory, float *scratch);

    const int8_t *Get(int table, int wave) const { return waves_[table * kWaves + wave]; }

    static const char *const kNames[kOwnTables]; // the AKWF bank's are akwf::kNames

  private:
    Wave *waves_ = nullptr;
};

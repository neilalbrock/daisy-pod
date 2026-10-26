// Grainfall - Space, the reverb after the effect
//
// The plate in common/plate.h, with a mono input, no shimmer, and a slow
// sine moving its modulated allpasses a quarter cycle apart.
//
// Four modes, as on the Microcosm, set the size, decay, damping, predelay
// and modulation: A Bright Room, B Dark Medium, C Large Hall, D Ambient. A
// mode change fades the reverb out, empties it (a little a sample, about
// 2 ms in all, so the callback doesn't overrun) and fades back in with the
// new size: the old tail goes, but changing lengths under a tail would
// click as it came round. Space mixes the reverb in, equal power: 0 is dry,
// 1 reverb only.
//
// The lines live in caller memory that reads fast (RAM_D2): there are
// about 30 reads a sample. libDaisy-free.

#pragma once

#include <cstddef>

#include "plate.h"

class Space
{
  public:
    enum Mode
    {
        kBrightRoom,
        kDarkMedium,
        kLargeHall,
        kAmbient,
    };
    static constexpr int kNumModes = 4;

    // The memory Init needs, in floats.
    static constexpr size_t kFloats = Plate::kFloats;

    // mem: kFloats floats. Init clears it, so call it before audio starts.
    void Init(float *mem);

    // Once per block.
    void SetMix(float space); // 0..1
    void SetMode(int mode);   // 0..3

    void Process(float *l, float *r);

  private:
    void Apply(int mode);

    Plate plate_;
    float gain_      = 0.6f;
    float lfo_phase_ = 0.0f;

    int   mode_ = -1, target_mode_ = kLargeHall;
    float fade_ = 0.0f; // the reverb fades out and in for a mode change
    float mix_ = 0.0f, mix_target_ = 0.0f;
};

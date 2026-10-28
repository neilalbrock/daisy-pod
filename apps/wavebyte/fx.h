// Wavebyte - the effects chain
//
// Two global slots, as on the Protein, each running one of the nine
// effects (fx_effects.h) or none. Routing sends the voices dry, through
// FX 1 then FX 2, or through FX 2 only. A type can be in one slot at a
// time: if FX 2 is set to FX 1's type it stays silent (dry), where the
// Protein won't offer it.
//
// A change of type or routing fades the slot out (10 ms), starts the new
// effect afresh (the reverb's tank is emptied over a few blocks first) and
// fades it in, so nothing clicks or replays old echoes.
//
// Set() once per block, then Process(). libDaisy-free.

#pragma once

#include <cstddef>
#include <cstdint>

#include "fx_effects.h"
#include "fx_params.h"
#include "plate.h"

class FxChain
{
  public:
    // Memory from the caller: fast (RAM_D2) for the reverb, chorus,
    // flanger and look-ahead lines, which are read many times a sample;
    // slow (SDRAM) for the delay's, read twice.
    static constexpr size_t kFastFloats = Plate::kFloats + 2 * (fx::Chorus::kLine + fx::Flanger::kLine
                                                                + fx::Compressor::kLine);
    static constexpr size_t kSlowFloats = 2 * fx::Delay::kLine;

    struct Settings
    {
        FxType       type[2];
        FxRouting    routing;
        const float *values;           // kNumFxParams, 0..1
        float        samples_per_beat; // the tempo, for Delay Sync
    };

    void Init(float *fast, float *slow, float sample_rate, size_t block_size);

    void Set(const Settings &s, size_t size);

    // In place.
    void Process(float *l, float *r, size_t size);

    // What each slot is running (kFxOff while it changes).
    FxType Running(int slot) const { return slots_[slot].fade > 0.0f ? slots_[slot].running : kFxOff; }

  private:
    struct Slot
    {
        FxType running  = kFxOff;
        float  fade     = 0.0f; // 0 dry .. 1 the effect
        float  target   = 0.0f;
        bool   clearing = false; // the reverb's tank is being emptied
    };

    void Start(FxType type);
    void SetEffect(FxType type, const float *values, size_t size);
    void Run(FxType type, float *l, float *r, size_t size);

    fx::Context    context_ = {48000.0f, 3000.0f, 24000.0f};
    float          fade_step_ = 0.002f; // per sample: 10 ms
    Slot           slots_[2];
    fx::Chorus     chorus_;
    fx::Flanger    flanger_;
    fx::Phaser     phaser_;
    fx::Tremolo    tremolo_;
    fx::Drive      drive_;
    fx::Compressor compressor_;
    fx::Eq         eq_;
    fx::Delay      delay_;
    fx::Reverb     reverb_;
};

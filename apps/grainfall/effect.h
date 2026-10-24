// Grainfall - effect interface
//
// Each of the 11 effects is a scheduler: once per block it looks at the
// controls, the tempo grid and recent onsets, and starts or steers voices.
// Effects that need per-sample work of their own (the delays) also get
// Process(). Nothing here allocates.

#pragma once

#include <cstdint>

#include "dsp.h"
#include "onset.h"
#include "voice.h"

struct EffectContext
{
    // Controls
    int   variation = 0;    // 0..3 = A..D
    float activity  = 0.5f; // 0..1
    float repeats   = 0.5f; // 0..1
    Env   shape     = Env::kSquare;
    bool  reverse   = false;

    // Time
    float   sample_rate  = 48000.0f;
    int     block_frames = 4;
    float   beat_frames  = 24000.0f; // one beat
    float   step_frames  = 24000.0f; // one subdivision step (Time)
    bool    step         = false;    // a step starts in this block
    int32_t step_index   = 0;        // steps since the tempo's wrap point
    float   step_pos     = 0.0f;     // the same, with the fraction through the step

    // Input
    bool            onset = false; // a note started since the last block
    const OnsetLog *onsets = nullptr;
    float           input_level = 0.0f; // envelope of the effect input
    uint32_t        write_pos = 0; // sample buffer write head
    uint32_t        buffer_frames = 0;

    VoicePool *voices = nullptr;
    dsp::Rng  *rng    = nullptr;

    // Start frame for a slice of `length` frames ending `delay` frames
    // behind the write head; the slice is already fully recorded.
    uint32_t SliceStart(float length, float delay) const
    {
        return write_pos - (uint32_t)(length + delay) - 2;
    }
};

class Effect
{
  public:
    // Called when the effect (or its variation) is selected. The engine
    // has already faded out every voice.
    virtual void Reset() {}

    virtual void Block(const EffectContext &ctx) = 0;

    // How much of the wet output to record back into the buffer for a
    // given Repeats setting. What matters is the gain round the loop, so an
    // effect that scales its voices down may ask for more than 1 (up to 1.5).
    virtual float Feedback(float) const { return 0.0f; }

    // Once per sample, for effects that work on the signal directly (the
    // delays): reads `buf` and the effect input, adds to *l and *r, and may
    // add its own feedback to *fb_l and *fb_r, which are recorded into the
    // buffer with this sample.
    virtual void Process(const SampleBuffer &, float, float, float *, float *, float *, float *) {}

  protected:
    ~Effect() = default;
};

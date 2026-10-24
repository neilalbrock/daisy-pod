// Grainfall - engine
//
// Signal flow, in stereo:
//
//   in -> Hold (live, or the held loop) -> sample buffer -> effect voices
//      -> pitch mod -> low-pass -> effect volume -> wet out
//
// The input also feeds an onset detector, so effects can pick out notes.
// Dry/wet mix is left to the caller. All memory is supplied by the caller
// (the buffers belong in SDRAM) and nothing allocates, so it is safe to run
// from the audio callback. libDaisy-free, so it also builds on a desktop.

#pragma once

#include <cstddef>
#include <cstdint>

#include "dsp.h"
#include "effect.h"
#include "fx_cloud.h"
#include "hold.h"
#include "onset.h"
#include "post_fx.h"
#include "sample_buffer.h"
#include "voice.h"

class GrainEngine
{
  public:
    // In the order the encoder steps through them, as on the Microcosm.
    enum class EffectId
    {
        kMosaic,
        kSeq,
        kGlide,
        kHaze,
        kTunnel,
        kStrum,
        kBlocks,
        kInterrupt,
        kArp,
        kPattern,
        kWarp,
    };
    static constexpr int kNumEffects    = 11;
    static constexpr int kNumVariations = 4;
    static constexpr int kNumSubdivs    = 6;

    struct Params
    {
        EffectId effect    = EffectId::kHaze;
        int      variation = 0;    // 0..3 = A..D
        float    activity  = 0.5f; // 0..1
        float    repeats   = 0.5f; // 0..1
        int      shape     = 0;    // 0..3: square, ramp up, triangle, ramp down
        int      subdiv    = 2;    // 0..5: 1/4, 1/2, TAP, 2x, 4x, 8x
        bool     reverse   = false;
        bool     hold      = false;

        // From Tempo, at the start of the block
        float samples_per_beat = 24000.0f;
        float beat_pos         = 0.0f;

        PostFx::Params post;
    };

    // buffer: buffer_frames * 2 floats; hold_buffer: hold_frames * 2 floats.
    // Both frame counts must be powers of two. The hold buffer must be
    // longer than the longest hold (2 s) plus 20 ms.
    void Init(float  sample_rate,
              float *buffer,
              size_t buffer_frames,
              float *hold_buffer,
              size_t hold_frames);

    // Once per block, before Process(): takes the controls and lets the
    // current effect schedule its voices.
    void BeginBlock(const Params &p, int block_frames);

    void Process(float in_l, float in_r, float *out_l, float *out_r);

    int NumVoices() const { return voices_.NumActive(); }

  private:
    Effect *EffectFor(EffectId id);

    float sample_rate_ = 48000.0f;

    SampleBuffer  buffer_;
    VoicePool     voices_;
    Hold          hold_;
    OnsetDetector onset_detector_;
    OnsetLog      onsets_;
    PostFx        post_;
    dsp::Rng      rng_;

    FxCloud cloud_;

    Params  params_;
    Effect *effect_     = nullptr;
    bool    first_      = true;
    int32_t last_step_  = -1;
    bool    onset_      = false; // seen since the last block
    float   hold_len_   = 0.0f;  // frames, used when Hold engages
    uint32_t preroll_   = 0;     // onsets are logged this far before detection
};

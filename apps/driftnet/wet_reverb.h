// Driftnet - Reverb mode
//
// A dense cluster of echoes that smears into a wash. Eight taps a side read
// a stereo line, spread unevenly and differently left and right; the last
// tap of each side feeds back into the other, so the cluster recurs and
// decays. TIME sets the size (where the taps sit) and the decay together;
// moving it slides the taps, which bends the pitch of what's sounding.
//
// MODIFY is smear: everything written to the line first passes a chain of
// four allpass diffusers, from bypassed at min (a clean multi-tap echo) to a
// gain of 0.75 at max (reverb). The diffusers sit in the feedback loop, so
// each pass smears further. Freeze cuts the input and holds the decay at 1;
// the diffusers keep smearing what's frozen. Blending in the undiffused
// signal isn't lossless, so a frozen loop would slowly fade: while frozen
// the blend glides to fully diffused, and MODIFY sets only the gain.
//
// SPREAD scatters the taps across the stereo field, as far as WIDTH lets
// them. CLASSIC stops the modulation and adds a short comb, so trails ring
// a little, like MOOD MKI.
//
// Timed in core frames, so the CLOCK scales it, and runs once per core tick.
// libDaisy-free.

#pragma once

#include <cstddef>
#include <cstdint>

class WetReverb
{
  public:
    // The diffusers' memory, in floats, which follows the line in Init's data.
    static constexpr size_t kDiffuserFloats = 3400;

    // data: frames * 2 floats for the line (interleaved), then
    // kDiffuserFloats for the diffusers, all in fast RAM. Any length longer
    // than the largest size (24000 frames); it needn't be a power of two.
    void Init(float *data, size_t frames);

    // Once per block.
    void SetRate(float core_rate); // for how fast the size follows TIME
    void SetTime(float time);      // 0..1
    void SetModify(float modify);  // 0..1
    void SetFreeze(bool on) { freeze_ = on; }
    void SetSpread(float width) { spread_ = width; } // 0: off
    void SetClassic(bool on) { classic_ = on; }

    // Forget what's in the line and the diffusers, without clearing them:
    // switching back to Reverb doesn't bring old tails back.
    void Reset();

    void Process(float in_l, float in_r, float *out_l, float *out_r);

  private:
    static constexpr int kTaps   = 8;
    static constexpr int kStages = 4;
    static constexpr int kModDepth = 6; // frames, on each side's last diffuser

    // One allpass on a short line after the main one. The last stage of each
    // side reads with a slowly moving fractional delay. Like the main line,
    // it isn't cleared on Reset(): it reads silence until it has filled
    // again. (Clearing costs a whole block: newlib's memset goes a byte at
    // a time.)
    struct Allpass
    {
        float   *line;
        uint32_t len, pos;
        uint32_t filled; // frames written since Reset(), up to len + 1
    };

    float Diffuse(int ch, float x);
    float Tap(int ch, float delay) const; // 0 until the line holds that much

    // The line: a ring of interleaved frames, sized to fit the fast RAM it's
    // in rather than to a power of two.
    float   *line_   = nullptr;
    uint32_t frames_ = 0, write_ = 0;
    uint32_t fresh_  = 0; // frames written since Reset()

    Allpass stages_[2][kStages];
    float   lfo_[2] = {0.0f, 0.5f};

    float size_ = 0.0f, size_target_ = 0.0f, size_coef_ = 0.0f;
    float decay_ = 0.0f;
    float k_     = 0.0f; // allpass gain
    // How much of the diffusers' output is used.
    float smear_ = 0.0f, smear_target_ = 0.0f, smear_coef_ = 0.0f;
    float damp_[2] = {};
    bool  freeze_  = false;
    float spread_  = 0.0f;
    bool  classic_ = false;
    float rate_set_ = -1.0f, time_set_ = -1.0f; // the setters skip repeats
};

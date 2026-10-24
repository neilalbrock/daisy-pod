// Grainfall - Multidelay: Pattern and Warp
//
// Up to eight delay taps read straight from the sample buffer, at multiples
// of the Time step. The last active tap feeds back into the buffer, so the
// whole pattern repeats, a little darker each time.
//
//   Activity - how many taps play, 1 to 8
//   Repeats  - feedback from the last tap, from a single pass to nearly
//              endless
//   Shape    - tap levels across the pattern: square even, ramp up
//              swelling, triangle loudest in the middle, ramp down fading
//   Time     - the step the tap times are counted in
//   Reverse  - every tap plays backwards, in windows of up to one step
//
// Pattern: tap rhythms.
//   A  evenly spaced, a classic delay (one tap, plus Repeats, is a plain
//      echo)
//   B  dotted and syncopated
//   C  triplets
//   D  clusters
//
// Warp: taps half a step apart, each changed on the way out.
//   A  each tap's low-pass opens as you play harder, the later taps less
//   B  each tap through a resonant band-pass, rising tap by tap
//   C  taps pitch-shifted: octaves, fifths and fourths up and down
//   D  each tap swaps between itself and an octave up every two beats,
//      fading across

#pragma once

#include <cmath>

#include "dsp.h"
#include "effect.h"

// Reads the buffer a set delay behind the write head. At any speed other
// than 1, two heads sweep a window a half-window apart, crossfading, so the
// tap is pitch-shifted (or reversed, at -1) while staying the same delay
// behind on average.
class TapReader
{
  public:
    void Init(float sample_rate) { smooth_ = dsp::SmoothCoef(0.1f, sample_rate); }
    void Reset() { delay_ = -1.0f; }

    // delay and window in frames. A change of delay glides, like tape.
    void Set(float delay, float rate, float window)
    {
        target_  = delay;
        rate_    = rate;
        window_  = window;
        pitched_ = fabsf(rate - 1.0f) >= 0.001f;
        inc_     = fabsf(1.0f - rate) / window;
    }

    void Read(const SampleBuffer &buf, float *l, float *r);

    // Moves on a sample without reading, for a tap that's faded out.
    void Skip()
    {
        Glide();
        if(pitched_)
            phase_ = dsp::WrapPhase(phase_ + inc_);
    }

  private:
    void Glide()
    {
        if(delay_ < 0.0f)
            delay_ = target_;
        delay_ += smooth_ * (target_ - delay_);
    }

    float smooth_  = 0.0f;
    float target_  = 2.0f, delay_ = -1.0f;
    float rate_    = 1.0f, window_ = 2400.0f;
    float inc_     = 0.0f; // head phase per frame
    bool  pitched_ = false;
    float phase_   = 0.0f;
};

class MultiTap : public Effect
{
  public:
    static constexpr int kTaps = 8;

    void Init(float sample_rate);
    void Reset() override;
    void Process(const SampleBuffer &buf, float in_l, float in_r, float *l,
                 float *r, float *fb_l, float *fb_r) override;

  protected:
    enum class TapFilter
    {
        kNone,
        kLowPass,
        kBandPass,
    };

    // Sets up the taps from tap times in steps, for the subclass's Block().
    void Setup(const EffectContext &ctx, const float (&times)[kTaps]);

    TapReader taps_[kTaps];
    TapReader alt_[kTaps]; // Warp D's octave-up readers
    float     alt_mix_[kTaps] = {};
    bool      use_alt_        = false;

    TapFilter filter_ = TapFilter::kNone;
    dsp::Svf  svf_l_[kTaps], svf_r_[kTaps];

    float sample_rate_ = 48000.0f;
    int   count_       = 1;
    float gain_l_[kTaps] = {}, gain_r_[kTaps] = {};
    float rate_[kTaps]   = {};
    float delay_[kTaps]  = {}; // frames
    float window_        = 2400.0f;

    float feedback_ = 0.0f;
    float tone_     = 0.0f; // feedback low-pass coefficient
    float lp_l_ = 0.0f, lp_r_ = 0.0f;
    dsp::DcBlock dc_l_, dc_r_;
};

class FxPattern : public MultiTap
{
  public:
    void Block(const EffectContext &ctx) override;
};

class FxWarp : public MultiTap
{
  public:
    void Reset() override;
    void Block(const EffectContext &ctx) override;

  private:
    float env_    = 0.0f; // A: smoothed input level
    float phase_  = 0.0f; // D: crossfade
};

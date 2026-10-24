// Bypass crossfade
//
// Switching straight between the effect and the dry input clicks. This
// fades between them over a few milliseconds instead. Keep the effect
// running while bypassed so there's something to fade back to.
//
// Header only and libDaisy-free, like dsp.h.

#pragma once

class BypassFade
{
  public:
    void Init(float sample_rate, float fade_s = 0.005f)
    {
        inc_ = 1.0f / (fade_s * sample_rate);
        mix_ = 0.0f;
    }

    // Once per sample. *l and *r hold the effect output on the way in, and
    // the blend on the way out: the effect, the dry input, or between.
    void Process(bool bypassed, float dry_l, float dry_r, float *l, float *r)
    {
        mix_ += bypassed ? inc_ : -inc_;
        mix_ = mix_ < 0.0f ? 0.0f : (mix_ > 1.0f ? 1.0f : mix_);
        *l += (dry_l - *l) * mix_;
        *r += (dry_r - *r) * mix_;
    }

  private:
    float mix_ = 0.0f; // 0 = effect, 1 = bypassed
    float inc_ = 0.0f;
};

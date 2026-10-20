// Tapeworn - tape machine EQ profiles
//
// Each model is a playback frequency response: a high-pass for the low-end
// loss, a two-stage low-pass for the top end, and one bell for the
// machine's character (a head bump, a tinny speaker resonance). Switching
// models crossfades between two filter chains, so turning the encoder
// doesn't click.

#pragma once

#include "tape_dsp.h"

class TapeModel
{
  public:
    static constexpr int kNumModels = 13; // model 0 is flat (no EQ)

    static const char *Name(int model);

    void Init(float sample_rate);

    // Takes effect over ~30 ms. Changes during a crossfade wait for it to
    // finish, so fast encoder turns land on the last model chosen.
    void Select(int model);
    int  Selected() const { return pending_; }

    float Process(float x);

  private:
    struct Profile
    {
        const char *name;
        float       hp_hz;   // 0 = no high-pass
        float       lp_hz;   // 0 = no low-pass
        float       lp_q;
        float       peak_hz; // 0 = no bell
        float       peak_q;
        float       peak_db;
        float       gain_db; // level match against the unfiltered sound
    };

    struct Chain
    {
        tape::Biquad hp, lp1, lp2, peak;
        float        gain = 1.0f;
        bool         off  = true;

        void  Set(const Profile &p, float sample_rate);
        float Process(float x);
    };

    static const Profile kProfiles[kNumModels];

    float sample_rate_ = 48000.0f;
    Chain chains_[2];
    int   current_  = 0;    // index into chains_ of the chain fading in
    int   model_    = 0;    // model on chains_[current_]
    int   pending_  = 0;    // model to switch to once the fade is done
    float fade_     = 1.0f; // 0..1, weight of chains_[current_]
    float fade_inc_ = 0.0f;
};

// Wavebyte - a voice's filter
//
// After the Protein's model of the CEM filter chip: one of a 24 dB
// low-pass, a 12 dB low-pass, a 12 dB high-pass, a Drive in their place, or
// nothing. Only one runs at a time.
//
// Resonance past 0.8 makes every type sing on its own, as on the Protein,
// with both oscillators down. The 24 dB low-pass is a ladder of four
// zero-delay-feedback one-poles with a soft clip in the feedback loop
// (Zavalishin, The Art of VA Filter Design); past 0.8 its feedback passes
// the point where the loop oscillates, and the clip holds the oscillation at
// a steady level. The feedback climbs steeply past that point, since how
// quickly the oscillation builds depends on how far past it is: at full
// resonance it rings up in about 15 cycles. The input goes into the clip at
// half level, so a note playing doesn't saturate it and choke the feedback,
// and the gain lost to resonance is made up afterwards. The 12 dB modes are
// a state variable filter whose damping goes negative past 0.8, with a soft
// clip on its band-pass state to hold the level; the clip works in
// proportion to the cutoff, as the damping does, so the level doesn't
// change with cutoff. Both take in a little hiss to ring up from.
//
// Cutoff comes from a table of the one-pole gain g = tan(pi fc / fs) by
// octave (Init), which saves an exp2f and a tanf per voice per block.
//
// With Drive, Cutoff (and everything that moves it) sets the amount and
// Resonance picks the curve, split evenly between five: PNP (a transistor's
// uneven clipping), Tube (biased, so asymmetric), PickUp (folds back as the
// level rises, so it changes with the envelope), Diode (hard) and Crunch (a
// sine folder, FM-like at high amounts).
//
// Set() once per block, Process() per sample. libDaisy-free.

#pragma once

#include <cmath>
#include <cstdint>

#include "dsp.h"

class VoiceFilter
{
  public:
    enum Type
    {
        kLp24,
        kLp12,
        kHp,
        kDrive,
        kOff,
        kNumTypes,
    };

    enum DriveCurve
    {
        kPnp,
        kTube,
        kPickUp,
        kDiode,
        kCrunch,
        kNumCurves,
    };

    static constexpr float kOctaves = 10.0f; // cutoff range above 20 Hz

    static float CutoffHz(float octaves) { return 20.0f * exp2f(octaves); }

    // Once, before any voice runs: the cutoff table for this sample rate.
    static void Init(float sample_rate)
    {
        top_ = log2f(sample_rate * 0.45f / 20.0f);
        for(int i = 0; i <= kTableSize; i++)
        {
            float hz    = CutoffHz(top_ * (float)i / kTableSize);
            g_table_[i] = tanf(dsp::kPi * hz / sample_rate);
        }
    }

    void Reset()
    {
        s_[0] = s_[1] = s_[2] = s_[3] = 0.0f;
        ic1_ = ic2_ = 0.0f;
    }

    // octaves: the cutoff, 0..kOctaves above 20 Hz (with modulation it
    // can go past either end). resonance: 0..1.
    void Set(Type type, float octaves, float resonance, float sample_rate)
    {
        type_ = type;
        switch(type)
        {
            case kLp24:
            {
                float g = G(octaves);
                g_      = g / (1.0f + g);
                g2_     = g_ * g_;
                g3_     = g2_ * g_;
                g4_     = g2_ * g2_;
                // 4 is where the loop starts to oscillate.
                float r = dsp::Clamp(resonance, 0.0f, 1.0f);
                k_      = r < 0.8f ? 5.0f * r : 4.0f + kLadderPast * (r - 0.8f);
                inv_    = 1.0f / (1.0f + k_ * g4_); // the loop's solution, without a divide per sample
                // Past 4 the clip holds the level, rising with k: scale it back.
                comp_ = (1.0f + kLadderMakeup * fminf(k_, 4.0f)) / kLadderIn;
                if(k_ > 4.0f)
                    comp_ *= 4.0f / k_;
                break;
            }
            case kLp12:
            case kHp:
            {
                // Damping from 2 (no resonance) to 0 at 0.8, then negative.
                float r = dsp::Clamp(resonance, 0.0f, 1.0f);
                float g = G(octaves);
                k_      = r < 0.8f ? 2.0f * (1.0f - r / 0.8f) : -kSvfPast * (r - 0.8f) / 0.2f;
                a1_     = 1.0f / (1.0f + g * (g + k_));
                a2_     = g * a1_;
                a3_     = g * a2_;
                clip_   = fminf(kSvfClip * g, 1.0f);
                break;
            }
            case kDrive:
            {
                float amount = dsp::Clamp(octaves / kOctaves, 0.0f, 1.0f);
                gain_        = 1.0f + 30.0f * amount * amount;
                int c        = (int)(dsp::Clamp(resonance, 0.0f, 1.0f) * kNumCurves);
                curve_       = (DriveCurve)(c < kNumCurves ? c : kNumCurves - 1);
                break;
            }
            default: break;
        }
    }

    float Process(float x)
    {
        switch(type_)
        {
            case kLp24:
            {
                float G  = g_;
                float in = (x + Hiss()) * kLadderIn;
                float S  = (1.0f - G) * (g3_ * s_[0] + g2_ * s_[1] + G * s_[2] + s_[3]);
                float y4 = (g4_ * in + S) * inv_;
                float u  = Clip(in - k_ * y4);
                for(float &s : s_)
                {
                    float v = (u - s) * G;
                    u       = v + s;
                    s       = u + v;
                }
                return u * comp_;
            }
            case kLp12:
            case kHp:
            {
                float v3 = x + Hiss() - ic2_;
                float v1 = a1_ * ic1_ + a2_ * v3;
                float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
                ic1_     = 2.0f * v1 - ic1_;
                ic1_ += (Clip(ic1_) - ic1_) * clip_;
                ic2_     = 2.0f * v2 - ic2_;
                return type_ == kLp12 ? v2 : x - k_ * v1 - v2;
            }
            case kDrive: return 0.8f * Shape(x * gain_);
            default: return x;
        }
    }

  private:
    // Tuned with a host test: what rings on alone, and over a note.
    static constexpr float kLadderPast   = 10.0f;  // feedback past 4, at full resonance
    static constexpr float kLadderIn     = 0.5f;  // input level into the loop's clip
    static constexpr float kLadderMakeup = 0.5f;  // made up afterwards, per unit of feedback
    static constexpr float kSvfPast      = 0.6f;  // negative damping at full resonance
    static constexpr float kSvfClip      = 6.0f;  // how hard the clip holds, per unit of g
    static constexpr float kHiss         = 1e-3f; // noise in, for the oscillation to grow from
    static constexpr int   kTableSize    = 512;

    // The one-pole gain for a cutoff in octaves above 20 Hz.
    static float G(float octaves)
    {
        float x = dsp::Clamp(octaves / top_, 0.0f, 1.0f) * kTableSize;
        int   i = (int)x;
        if(i >= kTableSize)
            return g_table_[kTableSize];
        return dsp::Lerp(g_table_[i], g_table_[i + 1], x - (float)i);
    }

    // A little white noise, as an analog filter has, so it rings up from
    // silence quickly rather than from rounding error. An LCG is plenty.
    float Hiss()
    {
        hiss_ = hiss_ * 1664525u + 1013904223u;
        return (float)(int32_t)hiss_ * (kHiss / 2147483648.0f);
    }

    // The loops' saturation: a cubic, slope 1 at 0, flat at 1 from 1.5 on.
    // No divide, unlike dsp::SoftClip, as it runs per sample in every voice.
    static float Clip(float x)
    {
        x = dsp::Clamp(x, -1.5f, 1.5f);
        return x - (4.0f / 27.0f) * x * x * x;
    }

    float Shape(float v) const
    {
        switch(curve_)
        {
            case kPnp: return v >= 0.0f ? v / (1.0f + v) : 0.5f * v / (1.0f - 0.5f * v);
            case kTube: return dsp::SoftClip(v + 0.3f) - dsp::SoftClip(0.3f);
            case kPickUp: return v / (1.0f + 0.25f * v * v);
            case kDiode: return dsp::Clamp(v, -1.0f, 1.0f);
            case kCrunch:
            default:
            {
                float phase = v * 0.25f; // a quarter of a sine per unit
                return dsp::FastSin(phase - floorf(phase));
            }
        }
    }

    static inline float g_table_[kTableSize + 1] = {};
    static inline float top_                     = 10.0f; // octaves to 0.45 fs

    Type       type_  = kLp24;
    float      g_     = 0.5f, k_ = 0.0f, comp_ = 1.0f;
    float      g2_ = 0.25f, g3_ = 0.125f, g4_ = 0.0625f, inv_ = 1.0f;
    float      s_[4]  = {};
    float      a1_ = 1.0f, a2_ = 0.0f, a3_ = 0.0f, ic1_ = 0.0f, ic2_ = 0.0f, clip_ = 1.0f;
    uint32_t   hiss_  = 0x9E3779B9u;
    float      gain_  = 1.0f;
    DriveCurve curve_ = kPnp;
};

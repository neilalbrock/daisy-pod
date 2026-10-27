// Wavebyte - a voice's filter
//
// After the Protein's model of the CEM filter chip: one of a 24 dB
// low-pass, a 12 dB low-pass, a 12 dB high-pass, a Drive in their place, or
// nothing. Only one runs at a time.
//
// The 24 dB low-pass is a ladder of four zero-delay-feedback one-poles with
// a soft clip in the feedback loop, so resonance past about 0.8 sings on
// its own (Zavalishin, The Art of VA Filter Design). The 12 dB modes are
// the state variable filter from dsp.h.
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

    void Reset()
    {
        s_[0] = s_[1] = s_[2] = s_[3] = 0.0f;
        svf_.Reset();
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
                float hz = dsp::Clamp(CutoffHz(octaves), 20.0f, sample_rate * 0.45f);
                float g  = tanf(dsp::kPi * hz / sample_rate);
                g_       = g / (1.0f + g);
                k_       = 5.0f * dsp::Clamp(resonance, 0.0f, 1.0f);
                comp_    = 1.0f + 0.4f * k_; // the ladder loses bass as resonance rises
                break;
            }
            case kLp12:
            case kHp:
            {
                // Q climbs steeply near the top, ringing almost forever.
                float r = dsp::Clamp(resonance / 0.82f, 0.0f, 1.0f);
                float k = fmaxf(2.0f * (1.0f - r), 0.01f);
                svf_.Set(CutoffHz(octaves), 1.0f / k, sample_rate);
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
                float G = g_, G2 = G * G, G4 = G2 * G2;
                float in = x * comp_ + 1e-5f; // a whisper of offset, so it can sing from silence
                float S  = (1.0f - G) * (G2 * G * s_[0] + G2 * s_[1] + G * s_[2] + s_[3]);
                float y4 = (G4 * in + S) / (1.0f + k_ * G4);
                float u  = dsp::SoftClip(in - k_ * y4);
                for(float &s : s_)
                {
                    float v = (u - s) * G;
                    u       = v + s;
                    s       = u + v;
                }
                return u;
            }
            case kLp12:
            {
                float bp;
                return svf_.Process(x, &bp);
            }
            case kHp:
            {
                float bp;
                float lp = svf_.Process(x, &bp);
                return x - bp - lp; // bp comes scaled by 1/Q
            }
            case kDrive: return 0.8f * Shape(x * gain_);
            default: return x;
        }
    }

  private:
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

    Type       type_  = kLp24;
    float      g_     = 0.5f, k_ = 0.0f, comp_ = 1.0f;
    float      s_[4]  = {};
    dsp::Svf   svf_;
    float      gain_  = 1.0f;
    DriveCurve curve_ = kPnp;
};

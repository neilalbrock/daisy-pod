// Tapeworn - small DSP helpers shared by the tape engine's parts
//
// Header only, no allocation, nothing that touches libDaisy, so the engine
// also builds on a desktop for offline testing.

#pragma once

#include <cmath>
#include <cstdint>

namespace tape
{
constexpr float kPi    = 3.14159265359f;
constexpr float kTwoPi = 6.28318530718f;

inline float Clamp(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

inline float Lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

inline float DbToGain(float db)
{
    return powf(10.0f, db * 0.05f);
}

// Coefficient for a one-pole smoother reaching ~63 % in `seconds`.
inline float SmoothCoef(float seconds, float sample_rate)
{
    return 1.0f - expf(-1.0f / (seconds * sample_rate));
}

// sin(2 pi phase) for phase in 0..1, to about 0.1 %. Plenty for modulation,
// and much cheaper than sinf.
inline float FastSin(float phase)
{
    float t = phase * 2.0f - 1.0f; // sin(2 pi phase) = -sin(pi t)
    float y = 4.0f * t * (1.0f - fabsf(t));
    y       = 0.225f * (y * fabsf(y) - y) + y;
    return -y;
}

inline float WrapPhase(float phase)
{
    return phase >= 1.0f ? phase - 1.0f : phase;
}

// xorshift32: cheap and safe to call from the audio callback.
class Rng
{
  public:
    explicit Rng(uint32_t seed = 0x9E3779B9u) : state_(seed ? seed : 1u) {}

    void Seed(uint32_t seed) { state_ = seed ? seed : 1u; }

    uint32_t Next()
    {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }

    float Uni() { return (float)(Next() >> 8) * (1.0f / 16777216.0f); } // 0..1
    float Bi() { return Uni() * 2.0f - 1.0f; }                          // -1..1
    float Range(float lo, float hi) { return lo + (hi - lo) * Uni(); }

    // True on average `rate_hz` times a second when called once per sample.
    bool Chance(float rate_hz, float sample_rate)
    {
        return Uni() < rate_hz / sample_rate;
    }

  private:
    uint32_t state_;
};

// RBJ cookbook biquad, transposed direct form II.
class Biquad
{
  public:
    enum class Type
    {
        kLowPass,
        kHighPass,
        kPeak,
    };

    void Reset() { z1_ = z2_ = 0.0f; }

    void SetUnity()
    {
        b0_ = 1.0f;
        b1_ = b2_ = a1_ = a2_ = 0.0f;
    }

    void Set(Type type, float freq, float q, float gain_db, float sample_rate)
    {
        float w0    = kTwoPi * Clamp(freq, 10.0f, sample_rate * 0.45f) / sample_rate;
        float cw    = cosf(w0);
        float alpha = sinf(w0) / (2.0f * q);
        float b0, b1, b2, a0, a1, a2;
        switch(type)
        {
            case Type::kLowPass:
                b0 = (1.0f - cw) * 0.5f;
                b1 = 1.0f - cw;
                b2 = b0;
                a0 = 1.0f + alpha;
                a1 = -2.0f * cw;
                a2 = 1.0f - alpha;
                break;
            case Type::kHighPass:
                b0 = (1.0f + cw) * 0.5f;
                b1 = -(1.0f + cw);
                b2 = b0;
                a0 = 1.0f + alpha;
                a1 = -2.0f * cw;
                a2 = 1.0f - alpha;
                break;
            case Type::kPeak:
            default:
            {
                float A = powf(10.0f, gain_db / 40.0f);
                b0      = 1.0f + alpha * A;
                b1      = -2.0f * cw;
                b2      = 1.0f - alpha * A;
                a0      = 1.0f + alpha / A;
                a1      = -2.0f * cw;
                a2      = 1.0f - alpha / A;
                break;
            }
        }
        b0_ = b0 / a0;
        b1_ = b1 / a0;
        b2_ = b2 / a0;
        a1_ = a1 / a0;
        a2_ = a2 / a0;
    }

    float Process(float x)
    {
        float y = b0_ * x + z1_;
        z1_     = b1_ * x - a1_ * y + z2_;
        z2_     = b2_ * x - a2_ * y;
        return y;
    }

  private:
    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f;
    float z1_ = 0.0f, z2_ = 0.0f;
};

// First-order DC blocker, corner around 10 Hz at 48 kHz.
class DcBlock
{
  public:
    float Process(float x)
    {
        float y = x - x1_ + 0.9987f * y1_;
        x1_     = x;
        y1_     = y;
        return y;
    }

  private:
    float x1_ = 0.0f, y1_ = 0.0f;
};
} // namespace tape

// Small DSP helpers shared by the apps' engines
//
// Header only, no allocation, nothing that touches libDaisy, so engines built
// on it also compile on a desktop for offline testing.

#pragma once

#include <cmath>
#include <cstdint>

namespace dsp
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

// tanh-like soft clip: close to tanh up to |x| = 3, clamped beyond.
inline float SoftClip(float x)
{
    x = Clamp(x, -3.0f, 3.0f);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

inline float WrapPhase(float phase)
{
    return phase >= 1.0f ? phase - 1.0f : phase;
}

// 2^x to about 1e-7, far cheaper than exp2f: fine for pitch. The integer
// part goes straight into the float's exponent, the rest (-0.5..0.5) through
// a polynomial. x is clamped to +-126.
inline float FastExp2(float x)
{
    x       = Clamp(x, -126.0f, 126.0f);
    float n = roundf(x);
    float f = (x - n) * 0.69314718f; // e^f, by Taylor series
    float p = 1.0f + f * (1.0f + f * (0.5f + f * (0.16666667f + f * (0.041666667f + f * (0.0083333333f + f * 0.0013888889f)))));
    union
    {
        float    f;
        uint32_t i;
    } u     = {p};
    u.i += (uint32_t)(int32_t)n << 23;
    return u.f;
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
    // Multiplies rather than divides: it runs several times per sample.
    bool Chance(float rate_hz, float sample_rate)
    {
        return Uni() * sample_rate < rate_hz;
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

// State variable filter in topology-preserving form (Zavalishin). It stays
// well behaved while the cutoff moves, so it can be swept once per block.
class Svf
{
  public:
    void Reset() { ic1_ = ic2_ = 0.0f; }

    void Set(float freq, float q, float sample_rate)
    {
        float g = tanf(kPi * Clamp(freq, 10.0f, sample_rate * 0.45f) / sample_rate);
        k_      = 1.0f / q;
        a1_     = 1.0f / (1.0f + g * (g + k_));
        a2_     = g * a1_;
        a3_     = g * a2_;
    }

    // Saves a second tanf for the other channel of a stereo pair.
    void CopyCoefs(const Svf &other)
    {
        k_  = other.k_;
        a1_ = other.a1_;
        a2_ = other.a2_;
        a3_ = other.a3_;
    }

    // Returns the low-pass output. *bp gets the band-pass, scaled to unity
    // gain at the centre frequency.
    float Process(float x, float *bp)
    {
        float v3 = x - ic2_;
        float v1 = a1_ * ic1_ + a2_ * v3;
        float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
        ic1_     = 2.0f * v1 - ic1_;
        ic2_     = 2.0f * v2 - ic2_;
        *bp      = v1 * k_;
        return v2;
    }

  private:
    float k_ = 1.414f, a1_ = 1.0f, a2_ = 0.0f, a3_ = 0.0f;
    float ic1_ = 0.0f, ic2_ = 0.0f;
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
} // namespace dsp

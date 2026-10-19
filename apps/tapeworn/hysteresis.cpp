#include "hysteresis.h"

#include <cmath>

#include "tape_dsp.h"

using namespace tape;

namespace
{
constexpr size_t kPhaseTaps = 16; // non-zero taps in the halfband's even phase
constexpr size_t kUpDelay   = 7;  // centre tap position, in input samples

// Modified Bessel function of the first kind, order 0, for the Kaiser window.
float BesselI0(float x)
{
    float sum = 1.0f, term = 1.0f;
    for(int k = 1; k < 20; k++)
    {
        term *= (x / (2.0f * k)) * (x / (2.0f * k));
        sum += term;
    }
    return sum;
}
} // namespace

void Hysteresis::Init(float sample_rate)
{
    t_ = 1.0f / (sample_rate * 2.0f);

    // Kaiser-windowed halfband lowpass. Only the even-index taps are non-zero
    // apart from the centre one (0.5), so that's all we keep.
    constexpr float kBeta   = 7.0f;
    const int       centre  = (int)kHalfbandTaps / 2;
    float           sum     = 0.0f;
    for(size_t i = 0; i < kPhaseTaps; i++)
    {
        int   n   = (int)(2 * i) - centre; // odd offset from the centre
        float x   = kPi * n * 0.5f;
        float r   = (float)n / centre;
        float win = BesselI0(kBeta * sqrtf(1.0f - r * r)) / BesselI0(kBeta);
        up_taps_[i] = sinf(x) / x * win; // 2 * h[n]: the upsampler's gain of 2
        sum += up_taps_[i];
    }
    for(auto &tap : up_taps_)
        tap /= sum; // exact unity gain at DC

    Reset();
    SetAmount(0.0f);
}

void Hysteresis::Reset()
{
    m_n1_ = h_n1_ = hd_n1_ = 0.0f;
    for(auto &s : up_hist_)
        s = 0.0f;
    for(auto &s : down_hist_)
        s = 0.0f;
    up_pos_ = down_pos_ = 0;
}

void Hysteresis::SetAmount(float amount)
{
    float s = Clamp(amount, 0.0f, 1.0f);

    // The tape is fixed; the knob sets how hard it's recorded. The curve
    // keeps more of the sweep in the subtle range.
    float u = powf(s, 1.3f);
    drive_  = DbToGain(-6.0f + 30.0f * u);

    // Fitted offline so a 0.3 peak sine keeps about the same level across the
    // sweep, then trimmed ~5 dB at the top, where peaky material (plucks,
    // drums) otherwise came out louder. Quiet input gets louder as it
    // compresses.
    makeup_ = DbToGain(9.0f - 38.6f * u + 11.4f * u * u);

    // Past ~80 % the record bias drifts off: the loop goes lopsided.
    float mis = Clamp((s - 0.8f) / 0.2f, 0.0f, 1.0f);
    bias_     = 0.6f * mis * mis;
}

float Hysteresis::Dmdt(float m, float h, float h_d) const
{
    float q = (h + alpha_ * m) / a_;
    float l, l_prime; // Langevin function and its derivative
    if(fabsf(q) < 0.1f)
    {
        // coth(q) - 1/q cancels badly in float near zero; use the series.
        l       = q / 3.0f - q * q * q / 45.0f;
        l_prime = 1.0f / 3.0f - q * q / 15.0f;
    }
    else
    {
        float coth = 1.0f / tanhf(q);
        l          = coth - 1.0f / q;
        l_prime    = 1.0f / (q * q) - coth * coth + 1.0f;
    }

    float m_diff  = ms_ * l - m;
    float delta   = h_d >= 0.0f ? 1.0f : -1.0f;
    float delta_m = (delta > 0.0f) == (m_diff >= 0.0f) ? 1.0f : 0.0f;
    float nc      = 1.0f - c_;

    float f1 = nc * delta_m * m_diff / (nc * delta * k_ - alpha_ * m_diff);
    float f2 = c_ * ms_ / a_ * l_prime;
    float f3 = 1.0f - c_ * alpha_ * ms_ / a_ * l_prime;
    return h_d * (f1 + f2) / f3;
}

float Hysteresis::Solve(float h)
{
    // dH/dt by an alpha-damped trapezoidal rule, then one RK2 step for M.
    constexpr float kDAlpha = 0.75f;
    float h_d = (1.0f + kDAlpha) / t_ * (h - h_n1_) - kDAlpha * hd_n1_;

    float k1 = t_ * Dmdt(m_n1_, h_n1_, hd_n1_);
    float k2 = t_ * Dmdt(m_n1_ + k1 * 0.5f, (h + h_n1_) * 0.5f, (h_d + hd_n1_) * 0.5f);
    float m  = m_n1_ + k2;

    if(!std::isfinite(m) || fabsf(m) > ms_ * 2.0f)
    {
        // Shouldn't happen, but never let a bad step latch the output.
        m   = 0.0f;
        h_d = 0.0f;
    }
    m_n1_  = m;
    h_n1_  = h;
    hd_n1_ = h_d;
    return m;
}

float Hysteresis::Process(float x)
{
    float h = x * drive_ + bias_;

    // Upsample: the even output phase is the filtered sum, the odd phase is
    // just the delayed input (the centre tap).
    up_hist_[up_pos_] = h;
    float  h0         = 0.0f;
    size_t idx        = up_pos_;
    for(size_t i = 0; i < kPhaseTaps; i++)
    {
        h0 += up_taps_[i] * up_hist_[idx];
        idx = idx == 0 ? kPhaseTaps - 1 : idx - 1;
    }
    size_t mid = (up_pos_ + kPhaseTaps - kUpDelay) % kPhaseTaps;
    float  h1  = up_hist_[mid];
    up_pos_    = (up_pos_ + 1) % kPhaseTaps;

    float m0 = Solve(h0);
    float m1 = Solve(h1);

    // Downsample: same halfband, evaluated only for the sample we keep.
    down_hist_[down_pos_] = m0;
    down_pos_             = (down_pos_ + 1) % kHalfbandTaps;
    down_hist_[down_pos_] = m1;
    size_t newest         = down_pos_;
    down_pos_             = (down_pos_ + 1) % kHalfbandTaps;

    float y = 0.5f * down_hist_[(newest + kHalfbandTaps - kHalfbandTaps / 2) % kHalfbandTaps];
    for(size_t i = 0; i < kPhaseTaps; i++)
        y += 0.5f * up_taps_[i] * down_hist_[(newest + kHalfbandTaps - 2 * i) % kHalfbandTaps];

    return y * makeup_;
}

// Tapeworn - magnetic hysteresis saturation
//
// A Jiles-Atherton model of tape magnetisation: the input is the recording
// field H, the output is the tape's magnetisation M. Because M depends on
// where the tape has been as well as where H is now, quiet signals get a
// subtle smear and loud ones a soft, level-dependent squash. Solved with
// second-order Runge-Kutta at twice the sample rate, following the approach
// in Jatin Chowdhury's "Real-time Physical Modelling for Analog Tape
// Machines" (DAFx 2019).

#pragma once

#include <cstddef>

class Hysteresis
{
  public:
    void Init(float sample_rate);
    void Reset();

    // 0 is near clean, ~0.5 is tape-warm, 1 is blown out and mis-biased.
    // Cheap enough to call once per audio block.
    void SetAmount(float amount);

    float Process(float x);

  private:
    static constexpr size_t kHalfbandTaps = 31; // odd, (taps - 1) / 2 odd

    float Solve(float h);
    float Dmdt(float m, float h, float h_d) const;

    float t_ = 1.0f / 96000.0f; // solver step, at the oversampled rate

    // Jiles-Atherton constants, from ChowTape's defaults at mid drive,
    // saturation and width.
    static constexpr float ms_    = 1.25f;      // saturation magnetisation
    static constexpr float a_     = ms_ / 3.01f; // anhysteretic shape
    static constexpr float alpha_ = 1.6e-3f;    // inter-domain coupling
    static constexpr float k_     = 0.47875f;   // coercivity (loop width)
    static constexpr float c_     = 0.6971f;    // reversibility

    float drive_  = 1.0f; // input gain into H
    float bias_   = 0.0f; // DC added to H, for asymmetric mis-bias
    float makeup_ = 1.0f;

    float m_n1_ = 0.0f, h_n1_ = 0.0f, hd_n1_ = 0.0f;

    // 2x oversampling. The halfband filter's only non-zero odd-phase taps are
    // kept, plus the centre tap (always 0.5).
    float up_taps_[(kHalfbandTaps + 1) / 2]   = {};
    float up_hist_[(kHalfbandTaps + 1) / 2]   = {};
    float down_hist_[kHalfbandTaps]           = {};
    size_t up_pos_ = 0, down_pos_ = 0;
};

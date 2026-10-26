// Grainfall - Space, the reverb after the effect
//
// A plate after Dattorro ("Effect Design, Part 1", 1997). The input, summed
// to mono, passes a predelay, a bandwidth low-pass and four allpass
// diffusers into a tank of two cross-coupled halves, each a modulated
// allpass, a delay, a damping low-pass, an allpass and a second delay.
// Seven taps a side, spread over the tank, make a wide stereo output.
//
// Four modes, as on the Microcosm, set the size, decay, damping, predelay
// and modulation: A Bright Room, B Dark Medium, C Large Hall, D Ambient. A
// mode change fades the reverb out, empties it (a little a sample, about
// 2 ms in all, so the callback doesn't overrun) and fades back in with the
// new lengths: the old tail goes, but changing lengths under a tail would
// click as it came round. Space mixes the reverb in, equal power: 0 is dry,
// 1 reverb only.
//
// Lengths are for 48 kHz. The lines live in caller memory that reads fast
// (RAM_D2): there are about 30 reads a sample. libDaisy-free.

#pragma once

#include <cstddef>
#include <cstdint>

class Space
{
  public:
    enum Mode
    {
        kBrightRoom,
        kDarkMedium,
        kLargeHall,
        kAmbient,
    };
    static constexpr int kNumModes = 4;

    // The memory Init needs, in floats.
    static constexpr size_t kFloats = 41200;

    // mem: kFloats floats. Init clears it, so call it before audio starts.
    void Init(float *mem);

    // Once per block.
    void SetMix(float space); // 0..1
    void SetMode(int mode);   // 0..3

    void Process(float *l, float *r);

  private:
    struct Line
    {
        float   *buf = nullptr;
        uint32_t cap = 0, pos = 0; // pos: the next write

        void Write(float x)
        {
            buf[pos] = x;
            if(++pos == cap)
                pos = 0;
        }
        // `d` frames back: before a Write, 1 is the last frame written.
        float Read(uint32_t d) const
        {
            int32_t i = (int32_t)pos - (int32_t)d;
            return buf[i < 0 ? i + (int32_t)cap : i];
        }
        float ReadFrac(float d) const;
    };

    void  Apply(int mode);
    float Allpass(Line &line, uint32_t len, float g, float x);

    float *Take(Line &line, uint32_t cap, float *mem);

    float   *mem_   = nullptr;
    uint32_t clear_ = 0; // floats emptied so far for a mode change

    Line pre_;
    Line in_[4];
    Line mod_[2], delay1_[2], ap_[2], delay2_[2]; // the tank's halves

    // Lengths for the current mode, in frames.
    uint32_t pre_len_ = 1;
    uint32_t in_len_[4]  = {};
    float    mod_len_[2] = {};
    uint32_t delay1_len_[2] = {}, ap_len_[2] = {}, delay2_len_[2] = {};
    uint32_t taps_[2][7] = {};

    float bandwidth_ = 1.0f, damping_ = 0.0f, decay_ = 0.5f, diffusion2_ = 0.5f;
    float excursion_ = 0.0f, gain_ = 0.6f;
    float lfo_phase_ = 0.0f;

    float bw_lp_ = 0.0f, damp_lp_[2] = {}, cross_[2] = {};

    int   mode_ = -1, target_mode_ = kLargeHall;
    float fade_ = 0.0f; // the reverb fades out and in for a mode change
    float mix_ = 0.0f, mix_target_ = 0.0f;
};

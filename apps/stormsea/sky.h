// Stormsea - the sky: Rain, Lightning and Thunder
//
//   the sea's output -> the tail ring (SDRAM, about 11 s)
//   the tail ring -> rain drops (up to 12 at once) -+-> into the sea
//                 -> thunder (up to 8 at once)     -+-> heard, in the wet
//
// Everything here plays back what the sea has just been doing, so the
// weather is always made of the tail itself:
//
// Rain: drops, tiny grains (2 to 20 ms) of the last half second of the
// tail, at unison, a fifth or an octave up, each at a random position and
// level, through a high-pass that brightens them. From a few a second to a
// downpour.
//
// Lightning: strikes at random, and silently: lightning is seen (LED 1,
// the web page), and thunder is what's heard. Each strike keeps a slice
// of the tail as it was, for its thunder.
//
// Thunder: each strike's slice, after a delay set by Distance (0.15 s near
// to 5 s far, varied a little each time). The delay sets the rest, as it
// would outdoors: near thunder is loud and bright with a sharp attack, far
// thunder quieter, darker, longer, swelling in slowly. Rumble picks what
// it plays: the slice an octave down, two octaves down, or reversed (an
// octave down), swelling in towards you and stopping short.
//
// What falls into the sea rings there and stays in a frozen sea; the sea
// wall keeps it from piling up. The amounts come in already scaled by the
// storm's strength, so at Storm 0 nothing new starts (what's playing
// finishes). libDaisy-free.

#pragma once

#include <cstddef>
#include <cstdint>

#include "dsp.h"
#include "wander.h"

class Sky
{
  public:
    enum Rumble
    {
        kOctave,
        kTwoOctaves,
        kReverse,
        kNumRumbles,
    };

    static constexpr uint32_t kRingFrames = 1u << 19; // about 11 s: thunder reads up to 9.9 s back
    static constexpr size_t   kFloats     = 2 * kRingFrames;

    // mem: kFloats floats (SDRAM is fine: a handful of reads a sample).
    // Init clears it, so call it before audio starts.
    void Init(float sample_rate, size_t block_size, float *mem);

    // Once per block. rain, lightning: 0..1, scaled by the storm already;
    // distance: 0 near .. 1 far; rumble: what thunder plays; gust: 0..1,
    // the gusts now, which bring more strikes.
    void Set(float rain, float lightning, float distance, int rumble, float gust);

    // Once per sample, before the sea: what falls into it, and what's heard.
    void Render(float *into_l, float *into_r, float *out_l, float *out_r);

    // Once per sample, after the sea: its output, for the weather to come.
    void Record(float l, float r)
    {
        ring_[2 * pos_]     = l;
        ring_[2 * pos_ + 1] = r;
        pos_                = (pos_ + 1) & (kRingFrames - 1);
    }

    // 1 at a strike, falling away over about 60 ms: for LED 1 and the page.
    float Flash() const { return flash_; }

    // For the page: how hard it's raining (0..1), and how loud the thunder
    // rolling now is (0..1 for one at full).
    float Rain() const { return rain_; }
    float Thunder() const { return rolling_; }

  private:
    static constexpr int kDrops    = 12;
    static constexpr int kThunders = 8; // far thunder waits up to 6.5 s, so several are pending

    // A slice of the ring played back: a rain drop or thunder.
    struct Voice
    {
        int   wait;     // samples before it starts (thunder)
        int   left;     // samples still to play; 0: free
        int   length;   // samples in all
        float pos;      // ring frame being read
        float rate;     // frames a sample, negative to reverse
        float gain_l, gain_r;
        float env_step; // 1 / length
        float t;        // 0..1 through it
        float lp1, lp2, coef; // the voice's one-pole filters: states, coefficient
        float swell;          // thunder: the share of it swelling in
        float swell_in, swell_out; // and its envelope's half cycles a unit of t, in and out
    };

    float Read(float pos) const; // the ring's mid at a fractional frame
    Voice *Free(Voice *voices, int n);
    void   Strike(int rumble);

    float    sample_rate_ = 48000.0f, block_rate_ = 12000.0f;
    float   *ring_        = nullptr;
    uint32_t pos_         = 0;
    dsp::Rng rng_;

    Voice  drops_[kDrops]     = {};
    Voice  thunder_[kThunders] = {};
    Wander rumble_[kThunders]; // each thunder's roll
    float  roll_[kThunders] = {};

    float rain_ = 0.0f, rain_rate_ = 0.0f, strike_rate_ = 0.0f, distance_ = 0.5f;
    float drop_hp_ = 0.0f; // the drops' high-pass coefficient
    float flash_ = 0.0f, flash_decay_ = 0.0f;
    float rolling_ = 0.0f;
};

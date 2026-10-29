// Stormsea - the sky: Rain, Lightning and Thunder
//
//   the sea's output -> the tail ring (SDRAM, about 11 s)
//   the tail ring -> rain drops (up to 12 at once)  -+-> into the sea
//                 -> strikes and thunder (4 at once) -+-> heard, in the wet
//
// Everything here plays back what the sea has just been doing, so the
// weather is always made of the tail itself:
//
// Rain: drops, tiny grains (2 to 20 ms) of the last half second of the
// tail, at unison, a fifth or an octave up, each at a random position and
// level, through a high-pass that brightens them. From a few a second to a
// downpour.
//
// Lightning: strikes at random. Each takes the last 0.15 to 0.5 s of the
// tail and plays it back an octave up (Flash) or reversed (Glint), or one
// of the two at random (Fork), with a sharp attack and a quick decay,
// its top lifted so it's bright.
//
// Thunder: each strike's slice again, 0.2 to 3 s later, an octave down,
// low-passed, swelling in and rolling away; the later it comes, the
// further away it is, so the quieter and darker.
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
    enum Strike
    {
        kFlash,
        kGlint,
        kFork,
        kNumStrikes,
    };

    static constexpr uint32_t kRingFrames = 1u << 19; // about 11 s: thunder reads up to 4.5 s back
    static constexpr size_t   kFloats     = 2 * kRingFrames;

    // mem: kFloats floats (SDRAM is fine: a handful of reads a sample).
    // Init clears it, so call it before audio starts.
    void Init(float sample_rate, size_t block_size, float *mem);

    // Once per block. rain, lightning, thunder: 0..1, scaled by the storm
    // already; gust: 0..1, the gusts now, which bring more strikes.
    void Set(float rain, float lightning, float thunder, int strike, float gust);

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

  private:
    static constexpr int kDrops = 12;
    static constexpr int kBolts = 4;

    // A slice of the ring played back: a rain drop, a strike or thunder.
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
    };

    float Read(float pos) const; // the ring's mid at a fractional frame
    Voice *Free(Voice *voices, int n);
    void   StartStrike(int strike);

    float    sample_rate_ = 48000.0f, block_rate_ = 12000.0f;
    float   *ring_        = nullptr;
    uint32_t pos_         = 0;
    dsp::Rng rng_;

    Voice  drops_[kDrops]   = {};
    Voice  strikes_[kBolts] = {};
    Voice  thunder_[kBolts] = {};
    Wander rumble_[kBolts]; // each thunder's roll
    float  roll_[kBolts] = {};

    float rain_ = 0.0f, rain_rate_ = 0.0f, strike_rate_ = 0.0f, thunder_amount_ = 0.0f;
    float drop_hp_ = 0.0f, strike_hp_ = 0.0f; // the drops' high-pass and the strikes' treble corner
    float attack_step_ = 0.0f;                // a strike's attack, a sample
    float flash_ = 0.0f, flash_decay_ = 0.0f;
};

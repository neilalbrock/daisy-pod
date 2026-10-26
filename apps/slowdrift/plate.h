// Slowdrift - the plate
//
// A plate after Dattorro ("Effect Design, Part 1", 1997), as grainfall's
// Space, but with every setting free to move while it rings: the line
// lengths are fixed (the paper's, at 48 kHz), and decay, damping,
// bandwidth, diffusion, predelay and modulation are set each block. Nothing
// is ever cleared, so moving a setting never cuts the tail.
//
// The input comes as mid and side. Mid passes a predelay, a bandwidth
// low-pass and four allpass diffusers into both halves of the tank; side
// passes its own predelay and diffusers and goes into one half added and
// the other subtracted, as far as SetInputSide() lets it (0: a mono tank).
// Each half of the tank is a modulated allpass, a delay, a damping
// low-pass, an allpass and a second delay; the halves feed each other. The
// modulated allpasses read where the caller's LFO puts them, up to
// kMaxExcursion frames apart. Seven taps a side, spread over the tank, make
// the stereo output.
//
// Diffusion scales every allpass gain: at 0 the diffusers are bare delays
// and the tank a ring of echoes (Rain), at 1 it's the paper's. A decay gain
// of 1 holds the tank forever (Freeze).
//
// Shimmer pitch-shifts part of what one half feeds the other, so each trip
// round the tank goes up an octave more. It's inside the loop rather than
// fed back into the input: the shifter never adds level, so however long
// the decay, the tank can't run away.
//
// Timed in frames, so a lower tick rate stretches it: run Process() once
// per tick. The lines live in caller memory that reads fast (RAM_D2): there
// are about 40 reads a tick. libDaisy-free.

#pragma once

#include <cstddef>
#include <cstdint>

#include "pitch_shift.h"

class Plate
{
  public:
    static constexpr float kMaxPredelay  = 2400.0f; // frames: 50 ms at 48k
    static constexpr float kMaxExcursion = 200.0f;  // frames

    // The memory Init needs, in floats.
    static constexpr size_t kFloats = 43000;

    // mem: kFloats floats, and the shifter's (both in fast RAM). Init
    // clears them, so call it before audio starts.
    void Init(float *mem, float *shifter_mem);

    // The decay gain that dies away by 60 dB in this many frames.
    float DecayForRt60(float frames) const;

    // Once per block; the caller smooths them.
    void SetDecay(float gain) { decay_ = gain; }       // 0..1
    void SetDamping(float damping) { damping_ = damping; } // 0 bright .. 1 dark
    void SetBandwidth(float bw) { bandwidth_ = bw; }   // 1 open .. 0 dark
    void SetDiffusion(float diffusion);                // 0 echoes .. 1 the paper's
    void SetPredelay(float frames);                    // 0..kMaxPredelay
    void SetExcursion(float frames);                   // 0..kMaxExcursion
    void SetInputSide(float side) { in_side_ = side; } // 0 mono .. 1 stereo
    void SetShimmer(float shimmer) { shimmer_ = shimmer; } // 0..1, the share shifted

    // Once per tick. lfo0, lfo1: -1..1, each half's modulated allpass.
    void Process(float mid, float side, float lfo0, float lfo1, float *l, float *r);

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

    static float *Take(Line &line, uint32_t cap, float *mem);
    static float  Allpass(Line &line, uint32_t len, float g, float x);
    float         Diffuse(Line *lines, float x);

    Line pre_[2], in_[2][4]; // mid and side
    Line mod_[2], delay1_[2], ap_[2], delay2_[2]; // the tank's halves

    uint32_t in_len_[4]  = {};
    float    mod_len_[2] = {};
    uint32_t delay1_len_[2] = {}, ap_len_[2] = {}, delay2_len_[2] = {};
    uint32_t taps_[2][7] = {};
    float    loop_       = 1.0f; // frames round the whole tank

    float decay_ = 0.5f, damping_ = 0.0f, bandwidth_ = 1.0f;
    float in_gain_[4] = {}, diffusion1_ = 0.0f, diffusion_ = 1.0f;
    float predelay_ = 1.0f, excursion_ = 0.0f, in_side_ = 0.0f, shimmer_ = 0.0f;

    PitchShift up_; // an octave

    float bw_lp_[2] = {}, damp_lp_[2] = {}, cross_[2] = {};
};

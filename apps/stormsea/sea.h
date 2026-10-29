// Stormsea - the sea: a feedback delay network
//
//   mid  -> 4 allpass diffusers -+
//   side -> 4 allpass diffusers -+-> into all 8 lines, a sign pattern each
//
//   each line: delay (Size; Depth and Wind swing it)
//              -> damping (decay and Tone) -> DC block
//              [-> Surge: gain past unity, soft clip]
//   the 8 lines [-> Undertow: four pairs turned] -> Hadamard matrix
//              [-> Undertow: two lines' feed sinking] x the sea wall
//              -> back into the lines
//   the lines' outputs, two sign patterns -> left and right
//
// Eight delay lines of different lengths, 21 ms to 137 ms at full Size,
// feed each other through an 8 x 8 Hadamard matrix. The matrix is
// orthogonal: it moves energy between the lines without adding or losing
// any, so the decay is set by each line's own gain alone, and a gain of 1
// holds the sea forever at the same level. Every input sample reaches
// every line within a trip or two, so the tail is dense from the start;
// the input diffusers smear each note before it goes in, so the first
// echoes don't stand out.
//
// Each line's damping is a one-pole low-pass whose gain at DC and at
// Nyquist give that line the decay times asked for (Jot's method): the top
// dies away in a fraction of the decay time, the same for every line
// whatever its length, so the tail darkens evenly.
//
// Surge raises each line's gain past unity into a soft clipper, so the
// tail swells up to the clipper and boils there instead of dying away.
//
// Undertow turns four pairs of lines into each other by slowly wandering
// angles before the matrix. A rotation is orthogonal, and so is the
// product of two orthogonal matrices, so the sea still neither gains nor
// loses: energy only moves between lines of different lengths, and the
// tail's colours shift and churn. And two lines' feed always passes a
// frequency shifter (freq_shift.h), which Undertow turns down by a
// fraction of a hertz, so each trip round lowers every partial a little
// and long tails slide down into the deep. A pitch shifter there, blended
// with the dry feed, lost power wherever the two decorrelated, and
// drained a frozen sea in a minute; the frequency shifter keeps it.

// The sea wall is a slow level control in the loop. It reads the lines'
// level each block and, over a ceiling, takes a little off each trip
// round, so an infinite sea fed with more and more input settles instead
// of growing. Below the ceiling it does nothing.
//
// Each line's read swings by up to kMaxMod frames: on a slow sine of its
// own (Depth), and at random (Wind's turbulence, a Wander a line, eased
// sample by sample between blocks). It's read with first-order allpass
// interpolation, which passes every frequency at unity. Cubic
// interpolation dulls the top a little every trip: in a frozen sea that
// took everything above 8 kHz away within seconds. The allpass clicks
// faintly each time its whole-frame delay steps: below -90 dB at Depth's
// slow swings, louder in Wind's.
//
// The lines live in caller memory that reads fast (RAM_D2): about 24
// reads a sample. libDaisy-free.

#pragma once

#include <cstddef>
#include <cstdint>

#include "freq_shift.h"
#include "wander.h"

namespace sea_detail
{
constexpr size_t Sum(const int *a, int n)
{
    size_t s = 0;
    for(int i = 0; i < n; i++)
        s += (size_t)a[i];
    return s;
}
} // namespace sea_detail

class Sea
{
  public:
    static constexpr int kLines     = 8;
    static constexpr int kDiffusers = 4; // per input, mid and side

    // The lines' lengths at full Size, in frames at 48 kHz: primes, spread
    // evenly in log, so their echoes never line up.
    static constexpr int kLengths[kLines] = {1031, 1327, 1741, 2269, 2953, 3851, 5023, 6599};
    static constexpr int kDiffuserLengths[2][kDiffusers] = {
        {211, 163, 547, 409}, // mid
        {233, 149, 571, 383}, // side
    };

    // Room past each line's length for its read to swing, in frames, and
    // a few more for the interpolation.
    static constexpr int   kModFrames = 960;
    static constexpr int   kSpare     = kModFrames + 8;
    static constexpr float kMaxMod    = (float)kModFrames;

    static constexpr float kMinSize   = 0.3f;
    static constexpr float kSurgeGain = 0.25f;

    // The memory Init needs, in floats.
    static constexpr int kTurns = 4; // pairs of lines Undertow turns
    static constexpr int kSinks = 2; // lines whose feed sinks

    static constexpr size_t kFloats = sea_detail::Sum(kLengths, kLines) + kLines * kSpare
                                      + sea_detail::Sum(kDiffuserLengths[0], kDiffusers)
                                      + sea_detail::Sum(kDiffuserLengths[1], kDiffusers);

    // mem: kFloats floats in fast RAM. Init clears it, so call it before
    // audio starts. The lengths are frames, so the sample rate is 48 kHz.
    void Init(float sample_rate, size_t block_size, float *mem);

    // Once per block; the caller smooths them. SetDecay after SetSize: it
    // works out each line's damping from its length.
    void SetSize(float size);                  // kMinSize..1, of the lines' full lengths
    void SetDecay(float rt60, float hf_ratio); // seconds (1e6 is as good as forever); the top's decay as a share of it
    void SetDepth(float frames);               // 0..kMaxMod / 4: each line's swing, each way

    // Wind's turbulence: each line's length wanders by up to `frames`
    // (0..kMaxMod / 4) each way, at random, its moves taking about 1.5 s
    // (speed 0) down to 0.1 s (speed 1).
    void SetTurbulence(float frames, float speed);

    // Surge, 0..1: each line's gain past unity, up to 1 + kSurgeGain,
    // into a soft clipper. 0 skips the clipper.
    void SetSurge(float surge);

    // Undertow, 0..1: how far the pairs turn (their angles wander up to
    // kMaxTurn each way) and how far the two lines' feed sinks a trip.
    void SetUndertow(float undertow);
    void SetInputSide(float side) { in_side_ = side; } // 0 mono .. 1 stereo

    // Once per block, after the block: the sea wall reads the level.
    void UpdateWall();

    // Once per sample. Out: unscaled, about the input's level at a few
    // seconds' decay.
    void Process(float mid, float side, float *l, float *r);

    float Wall() const { return wall_; }   // the wall's gain each trip, 1: not touching
    float Level() const { return level_; } // the lines' mean square, smoothed

  private:
    struct Line
    {
        float *buf;
        int    size;
        int    pos;        // the next write
        float  delay;      // frames, without the swing
        float  b, a, z;    // damping: z = b * x + a * z
        float  dc_x, dc_y; // DC block
        float  phase, inc; // the swing's sine
        float  ap_y;       // the allpass interpolator's last output
        float  wind, wind_step; // Wind's swing now, and its step a sample
        Wander turbulence;
    };

    struct Diffuser
    {
        float *buf;
        int    size, pos;
        float  gain;

        float Process(float x)
        {
            float delayed = buf[pos];
            float v       = x + gain * delayed;
            buf[pos]      = v;
            if(++pos >= size)
                pos = 0;
            return delayed - gain * v;
        }
    };

    static float Read(Line &line, float delay);

    float    sample_rate_ = 48000.0f;
    Line     lines_[kLines];
    Diffuser diff_[2][kDiffusers];

    // As the damping was last worked out for, to skip it when nothing moved.
    float set_size_ = 0.0f, set_rt60_ = 0.0f, set_hf_ratio_ = 0.0f;
    float size_     = 1.0f;
    float depth_    = 0.0f;
    float in_side_  = 1.0f;
    float surge_    = 0.0f, surge_gain_ = 1.0f;

    Wander     turn_wander_[kTurns];
    float      turn_c_[kTurns] = {}, turn_s_[kTurns] = {};
    bool       turning_    = false;
    FreqShift  sink_[kSinks];

    float block_frames_ = 4.0f;

    float energy_ = 0.0f; // summed since the last UpdateWall
    float level_  = 0.0f;
    float wall_   = 1.0f;
    float level_attack_ = 0.0f, level_release_ = 0.0f; // per block
    float block_scale_  = 0.0f;                         // 1 / (frames a block x lines)
};

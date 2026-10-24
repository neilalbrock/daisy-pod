// Grainfall - granular engine
//
// Records stereo input continuously into a circular buffer and plays it back
// as a cloud of short, windowed grains. All memory is supplied by the caller
// (the buffers belong in SDRAM) and Process() never allocates, so it is safe
// to call from the audio callback.

#pragma once

#include <cstddef>
#include <cstdint>

#include "dsp.h"

class GrainEngine
{
  public:
    static constexpr size_t kMaxGrains = 32;

    struct Params
    {
        float size_s      = 0.1f;  // grain length, seconds
        float density_hz  = 10.0f; // grains started per second
        bool  ext_trigger = false; // grains start only on Trigger(); density_hz
                                   // is then just the expected rate, for levels
        float position_s  = 0.2f;  // how far behind the record head grains start
        float spray_s     = 0.0f;  // random extra offset added to position
        float pitch       = 1.0f;  // playback rate (2 = up an octave)
        float spread      = 0.5f;  // random stereo placement, 0 = centred, 1 = hard L/R
        bool  freeze      = false; // stop recording, keep granulating what's held
    };

    // buf_l/buf_r: buffer_len samples each.
    void Init(float sample_rate, float *buf_l, float *buf_r, size_t buffer_len);

    // Read once per block from the audio callback; values are clamped.
    void SetParams(const Params &p);

    // Starts a grain now. Call before Process() for the sample it lands on.
    void Trigger() { StartGrain(); }

    // Records in_l/in_r (unless frozen) and returns the wet grain cloud.
    void Process(float in_l, float in_r, float *out_l, float *out_r);

  private:
    struct Grain
    {
        bool  active;
        float pos;       // read position in the buffer, fractional samples
        float inc;       // read increment per output sample (pitch)
        float phase;     // 0..1 through the window
        float phase_inc; // 1 / length in samples
        float gain_l, gain_r;
    };

    void  StartGrain();
    float Read(const float *buf, float pos) const;

    float  sample_rate_ = 48000.0f;
    float *buf_l_       = nullptr;
    float *buf_r_       = nullptr;
    size_t len_         = 0;
    size_t write_       = 0;

    Params   params_;
    Grain    grains_[kMaxGrains] = {};
    float    spawn_acc_          = 0.0f; // counts up to 1, then starts a grain
    float    norm_               = 1.0f; // output gain compensating for overlap
    dsp::Rng rng_;
};

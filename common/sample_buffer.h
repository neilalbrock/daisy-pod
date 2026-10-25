// Stereo circular recorder
//
// Frames are interleaved L/R so one cache line holds both channels of a
// read. The length is a power of two and positions are plain uint32_t frame
// counts that wrap with the mask, so a slice's start stays exact however far
// the write head has moved on. The memory belongs to the caller (SDRAM).
//
// Header only and libDaisy-free, like dsp.h.

#pragma once

#include <cstddef>
#include <cstdint>

class SampleBuffer
{
  public:
    // data: frames * 2 floats. frames must be a power of two.
    void Init(float *data, size_t frames)
    {
        data_  = data;
        mask_  = (uint32_t)frames - 1;
        write_ = 0;
        for(size_t i = 0; i < frames * 2; i++)
            data_[i] = 0.0f;
    }

    void Write(float l, float r)
    {
        float *f = data_ + 2 * write_;
        f[0]     = l;
        f[1]     = r;
        write_   = (write_ + 1) & mask_;
    }

    // The frame the next Write() fills. Everything before it is recorded.
    uint32_t WritePos() const { return write_; }
    uint32_t Frames() const { return mask_ + 1; }

    // A frame's two samples, to read or overwrite in place; any frame number
    // works, it wraps.
    float       *At(uint32_t frame) { return data_ + 2 * (frame & mask_); }
    const float *At(uint32_t frame) const { return data_ + 2 * (frame & mask_); }

    // Linear interpolation between `frame` and the one after; any frame
    // number works, it wraps.
    void Read(uint32_t frame, float frac, float *l, float *r) const
    {
        const float *a = data_ + 2 * (frame & mask_);
        const float *b = data_ + 2 * ((frame + 1) & mask_);
        *l             = a[0] + frac * (b[0] - a[0]);
        *r             = a[1] + frac * (b[1] - a[1]);
    }

  private:
    float   *data_  = nullptr;
    uint32_t mask_  = 0;
    uint32_t write_ = 0;
};

// Wavebyte - the effects
//
// The Protein's nine, our own versions from its manual's descriptions:
// Chorus, Flanger, Phaser, Tremolo, Drive, Compressor, EQ, Delay, Reverb.
// Each takes its type's run of values (fx_params.h, 0..1) once per block in
// Set(), working out its units there, and processes stereo in place, with
// no divides or transcendental calls per sample. Reset() starts it afresh
// as it comes into a slot; delay lines aren't cleared (too slow for the
// callback): a Ring counts what's been written since and reads silence
// before that.
//
// Where the manual leaves a parameter open, the choice is ours: Chorus
// Spread spreads its voices' delays (low: all alike, so it rings), and the
// Phaser's two models are a smooth sweep with its stages spread (Nave) and
// a stepped sweep, all stages together and the feedback inverted (PPG).
//
// libDaisy-free.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "dsp.h"
#include "fx_params.h"
#include "plate.h"
#include "voice_filter.h"

namespace fx
{
struct Context
{
    float sample_rate;
    float block_rate;
    float samples_per_beat; // for the synced delay
};

inline float Bipolar(float x)
{
    return 2.0f * x - 1.0f;
}

inline int Pick(float x, int n)
{
    int i = (int)(x * (float)n);
    return i < 0 ? 0 : (i >= n ? n - 1 : i);
}

// A one-pole low-pass coefficient for a cutoff in Hz.
inline float OnePole(float hz, float sample_rate)
{
    return 1.0f - dsp::FastExp2(-9.0647f * hz / sample_rate); // 1 - e^(-2 pi f / fs)
}

// log2 to about 0.01, for the compressor's level in decibels.
inline float FastLog2(float x)
{
    union
    {
        float    f;
        uint32_t i;
    } u = {x};
    float e = (float)(int)((u.i >> 23) & 255u) - 127.0f;
    u.i     = (u.i & 0x7FFFFFu) | 0x3F800000u; // the mantissa, 1..2
    float m = u.f;
    return e + (-0.34484843f * m + 2.02466578f) * m - 1.67487759f;
}

// The feedback paths' saturation: a cubic, flat at 1 from 1.5 on.
inline float Clip(float x)
{
    x = dsp::Clamp(x, -1.5f, 1.5f);
    return x - (4.0f / 27.0f) * x * x * x;
}

// A sine or triangle, -1..1, for phase in cycles (any range).
inline float Wave(float phase, int shape)
{
    phase -= floorf(phase);
    if(shape == 0)
        return dsp::FastSin(phase);
    return phase < 0.25f ? 4.0f * phase : (phase < 0.75f ? 2.0f - 4.0f * phase : 4.0f * phase - 4.0f);
}

// A delay line, power-of-two long, read between samples.
struct Ring
{
    float   *buf    = nullptr;
    uint32_t mask   = 0;
    uint32_t write  = 0;
    uint32_t filled = 0; // written since Reset, up to the length

    void Init(float *mem, uint32_t length)
    {
        buf  = mem;
        mask = length - 1;
        Reset();
    }
    void Reset() { filled = 0; }

    void Write(float x)
    {
        buf[write & mask] = x;
        write++;
        if(filled <= mask)
            filled++;
    }

    // `delay` samples before the newest written (0: the newest).
    float Read(float delay) const
    {
        uint32_t i = (uint32_t)delay;
        if(i + 2 > filled)
            return 0.0f;
        float t = delay - (float)i;
        float a = buf[(write - 1 - i) & mask], b = buf[(write - 2 - i) & mask];
        return a + (b - a) * t;
    }
};

// ---------------------------------------------------------------- Chorus

// 2-8 modulated delays, half on each side, their LFOs spread across the
// cycle; Center offsets the right side's LFOs from the left's.
class Chorus
{
  public:
    static constexpr uint32_t kLine = 2048; // 42 ms

    void Init(float *mem)
    {
        line_[0].Init(mem, kLine);
        line_[1].Init(mem + kLine, kLine);
    }
    void Reset()
    {
        line_[0].Reset();
        line_[1].Reset();
        fb_[0] = fb_[1] = lp_[0] = lp_[1] = hp_[0] = hp_[1] = 0.0f;
        for(float &d : delay_)
            d = 0.0f;
    }

    void Set(const float *v, const Context &c, size_t size)
    {
        static constexpr int kStages[] = {2, 4, 6, 8};
        mix_      = v[0];
        float depth = 6.0f * v[1] * 0.001f * c.sample_rate; // samples
        phase_ += 0.05f * dsp::FastExp2(8.0f * v[2]) / c.block_rate;
        phase_ -= floorf(phase_);
        feedback_ = 0.85f * Bipolar(v[3]);
        int shape = Pick(v[6], 2);
        stages_   = kStages[Pick(v[7], 4)] / 2; // per side
        hc_       = OnePole(20000.0f * dsp::FastExp2(-6.06f * v[8]), c.sample_rate); // 20 kHz to 300 Hz
        lc_       = OnePole(20.0f * dsp::FastExp2(6.64f * v[9]), c.sample_rate);     // 20 Hz to 2 kHz
        gain_     = 1.0f / (float)stages_;
        for(int side = 0; side < 2; side++)
            for(int k = 0; k < stages_; k++)
            {
                // Voices 7 ms apart at full Spread, from 7 ms.
                float base = (7.0f + 7.0f * v[5] * (float)k) * 0.001f * c.sample_rate;
                float lfo  = Wave(phase_ + (float)k / (float)stages_ + (side ? 0.5f * v[4] : 0.0f), shape);
                float to   = base + 0.5f * depth * (1.0f + lfo);
                int   i    = side * 4 + k;
                if(delay_[i] == 0.0f)
                    delay_[i] = to;
                step_[i] = (to - delay_[i]) / (float)size;
            }
    }

    void Process(float *l, float *r, size_t size)
    {
        float *io[2] = {l, r};
        for(size_t n = 0; n < size; n++)
            for(int side = 0; side < 2; side++)
            {
                float x = io[side][n];
                line_[side].Write(x + feedback_ * fb_[side]);
                float sum = 0.0f;
                for(int k = 0; k < stages_; k++)
                {
                    float &d = delay_[side * 4 + k];
                    d += step_[side * 4 + k];
                    sum += line_[side].Read(d);
                }
                float wet = sum * gain_;
                fb_[side] = wet;
                lp_[side] += (wet - lp_[side]) * hc_;
                hp_[side] += (lp_[side] - hp_[side]) * lc_;
                io[side][n] = dsp::Lerp(x, lp_[side] - hp_[side], mix_);
            }
    }

  private:
    Ring  line_[2];
    float delay_[8] = {}, step_[8] = {};
    float phase_ = 0.0f, mix_ = 0.5f, feedback_ = 0.0f, hc_ = 1.0f, lc_ = 0.0f, gain_ = 0.5f;
    float fb_[2] = {}, lp_[2] = {}, hp_[2] = {};
    int   stages_ = 2;
};

// ---------------------------------------------------------------- Flanger

class Flanger
{
  public:
    static constexpr uint32_t kLine = 1024; // 21 ms

    void Init(float *mem)
    {
        line_[0].Init(mem, kLine);
        line_[1].Init(mem + kLine, kLine);
    }
    void Reset()
    {
        line_[0].Reset();
        line_[1].Reset();
        fb_[0] = fb_[1] = delay_[0] = delay_[1] = 0.0f;
    }

    void Set(const float *v, const Context &c, size_t size)
    {
        mix_ = v[0];
        phase_ += 0.02f * dsp::FastExp2(9.0f * v[2]) / c.block_rate;
        phase_ -= floorf(phase_);
        feedback_   = 0.95f * Bipolar(v[3]);
        float most  = (0.3f + 9.7f * v[4]) * 0.001f * c.sample_rate; // the longest it sweeps to
        int   shape = Pick(v[6], 2);
        for(int side = 0; side < 2; side++)
        {
            float lfo = Wave(phase_ + (side ? 0.5f * v[5] : 0.0f), shape);
            float to  = fmaxf(most * (1.0f - v[1] * 0.5f * (1.0f + lfo)), 2.0f);
            if(delay_[side] == 0.0f)
                delay_[side] = to;
            step_[side] = (to - delay_[side]) / (float)size;
        }
    }

    void Process(float *l, float *r, size_t size)
    {
        float *io[2] = {l, r};
        for(int side = 0; side < 2; side++)
            for(size_t n = 0; n < size; n++)
            {
                float x = io[side][n];
                delay_[side] += step_[side];
                float wet = line_[side].Read(delay_[side]);
                line_[side].Write(x + feedback_ * fb_[side]);
                fb_[side]   = wet;
                io[side][n] = dsp::Lerp(x, wet, mix_);
            }
    }

  private:
    Ring  line_[2];
    float delay_[2] = {}, step_[2] = {}, fb_[2] = {};
    float phase_ = 0.0f, mix_ = 0.5f, feedback_ = 0.0f;
};

// ---------------------------------------------------------------- Phaser

// First-order allpasses, swept by the LFO, with feedback round them; the
// dry signal and theirs together make the notches.
class Phaser
{
  public:
    void Reset()
    {
        for(int side = 0; side < 2; side++)
        {
            fb_[side] = 0.0f;
            for(float &s : state_[side])
                s = 0.0f;
        }
    }

    void Set(const float *v, const Context &c, size_t)
    {
        static constexpr int kStages[] = {2, 4, 8, 12, 16};
        mix_ = v[0];
        phase_ += 0.02f * dsp::FastExp2(9.0f * v[2]) / c.block_rate;
        phase_ -= floorf(phase_);
        bool ppg  = Pick(v[9], 2) == 1;
        feedback_ = 0.9f * Bipolar(v[3]) * (ppg ? -1.0f : 1.0f);
        stages_   = kStages[Pick(v[8], 5)] / 2; // per side
        int shape = Pick(v[7], 2);
        float center = 100.0f * dsp::FastExp2(6.32f * v[4]); // 100 Hz to 8 kHz
        for(int side = 0; side < 2; side++)
        {
            float lfo = Wave(phase_ + (side ? 0.5f * v[6] : 0.0f), shape);
            if(ppg)
                lfo = floorf(lfo * 8.0f + 0.5f) * 0.125f; // a stepped sweep
            float hz = center * dsp::FastExp2(3.0f * v[1] * lfo);
            for(int k = 0; k < stages_; k++)
            {
                // Nave: the stages spread up to two octaves either side.
                float spread = ppg || stages_ < 2 ? 0.0f : 4.0f * v[5] * ((float)k / (float)(stages_ - 1) - 0.5f);
                float f      = fminf(hz * dsp::FastExp2(spread), 0.45f * c.sample_rate);
                float g      = tanf(dsp::kPi * f / c.sample_rate);
                coef_[side][k] = (1.0f - g) / (1.0f + g);
            }
        }
    }

    void Process(float *l, float *r, size_t size)
    {
        float *io[2] = {l, r};
        for(int side = 0; side < 2; side++)
        {
            float *s = state_[side];
            float *a = coef_[side];
            for(size_t n = 0; n < size; n++)
            {
                float x = io[side][n];
                float y = Clip(x + feedback_ * fb_[side]);
                for(int k = 0; k < stages_; k++)
                {
                    float out = s[k] - a[k] * y;
                    s[k]      = y + a[k] * out;
                    y         = out;
                }
                fb_[side]   = y;
                io[side][n] = dsp::Lerp(x, y, mix_);
            }
        }
    }

  private:
    float coef_[2][8] = {}, state_[2][8] = {}, fb_[2] = {};
    float phase_ = 0.0f, mix_ = 0.5f, feedback_ = 0.0f;
    int   stages_ = 2;
};

// ---------------------------------------------------------------- Tremolo

class Tremolo
{
  public:
    void Reset() { lfo_[0] = lfo_[1] = 0.0f; }

    void Set(const float *v, const Context &c, size_t size)
    {
        phase_ += 0.1f * dsp::FastExp2(7.0f * v[1]) / c.block_rate; // 0.1 to 12.8 Hz
        phase_ -= floorf(phase_);
        int   shape  = Pick(v[3], 3);
        // Smooth: a slew of up to 40 ms on the LFO.
        float smooth = v[4] > 0.0f ? OnePole(1.0f / (6.283f * 0.04f * v[4]), c.block_rate) : 1.0f;
        for(int side = 0; side < 2; side++)
        {
            float p   = phase_ + (side ? 0.5f * v[2] : 0.0f);
            float raw = shape == 2 ? ((p - floorf(p)) < 0.5f ? 1.0f : -1.0f) : Wave(p, shape);
            lfo_[side] += (raw - lfo_[side]) * smooth;
            float to   = 1.0f - v[0] * 0.5f * (1.0f + lfo_[side]);
            step_[side] = (to - gain_[side]) / (float)size;
        }
    }

    void Process(float *l, float *r, size_t size)
    {
        float *io[2] = {l, r};
        for(int side = 0; side < 2; side++)
        {
            float g = gain_[side];
            for(size_t n = 0; n < size; n++)
            {
                g += step_[side];
                io[side][n] *= g;
            }
            gain_[side] = g;
        }
    }

  private:
    float phase_ = 0.0f, lfo_[2] = {}, gain_[2] = {1.0f, 1.0f}, step_[2] = {};
};

// ---------------------------------------------------------------- Drive

// The filter's Drive curves (voice_filter.h), on the whole mix.
class Drive
{
  public:
    void Reset() {}

    void Set(const float *v, const Context &, size_t)
    {
        mix_   = v[0];
        gain_  = 1.0f + 30.0f * v[1] * v[1];
        curve_ = (VoiceFilter::DriveCurve)Pick(v[2], VoiceFilter::kNumCurves);
        out_   = 0.8f * dsp::FastExp2(Bipolar(v[3]) * 2.0f); // +-12 dB
    }

    void Process(float *l, float *r, size_t size)
    {
        float *io[2] = {l, r};
        for(int side = 0; side < 2; side++)
            for(size_t n = 0; n < size; n++)
            {
                float x     = io[side][n];
                io[side][n] = dsp::Lerp(x, out_ * VoiceFilter::DriveShape(curve_, x * gain_), mix_);
            }
    }

  private:
    float                   mix_ = 1.0f, gain_ = 1.0f, out_ = 0.8f;
    VoiceFilter::DriveCurve curve_ = VoiceFilter::kPnp;
};

// ---------------------------------------------------------------- Compressor

// Stereo-linked, on the peak level, in log2 (6 dB) units. Look Ahead
// delays the sound so the gain comes down before the peak arrives.
class Compressor
{
  public:
    static constexpr uint32_t kLine = 512; // 10.7 ms

    void Init(float *mem)
    {
        line_[0].Init(mem, kLine);
        line_[1].Init(mem + kLine, kLine);
    }
    void Reset()
    {
        line_[0].Reset();
        line_[1].Reset();
        env_ = 0.0f;
    }

    void Set(const float *v, const Context &c, size_t)
    {
        float ratio     = 1.0f + 19.0f * v[0] * v[0];
        slope_          = 1.0f - 1.0f / ratio;
        threshold_      = -40.0f * (1.0f - v[1]) / 6.0206f; // -40..0 dB
        in_             = dsp::FastExp2(Bipolar(v[2]) * 2.0f);
        float out_log2  = Bipolar(v[3]) * 2.0f;
        if(Pick(v[4], 2) == 1)
            out_log2 -= 0.5f * threshold_ * slope_; // Auto Gain: make up half what's taken at 0 dB
        out_            = dsp::FastExp2(out_log2);
        float attack_s  = 0.0001f * dsp::FastExp2(9.966f * v[5]);  // 0.1-100 ms
        float release_s = 0.01f * dsp::FastExp2(6.644f * v[6]);    // 10 ms-1 s
        attack_         = OnePole(1.0f / (6.283f * attack_s), c.sample_rate);
        release_        = OnePole(1.0f / (6.283f * release_s), c.sample_rate);
        look_           = v[7] * 0.010f * c.sample_rate;
    }

    void Process(float *l, float *r, size_t size)
    {
        for(size_t n = 0; n < size; n++)
        {
            float x0 = l[n] * in_, x1 = r[n] * in_;
            float level = fmaxf(fabsf(x0), fabsf(x1));
            env_ += (level - env_) * (level > env_ ? attack_ : release_);
            float over = FastLog2(env_ + 1e-9f) - threshold_;
            float gain = over > 0.0f ? dsp::FastExp2(-over * slope_) : 1.0f;
            line_[0].Write(x0);
            line_[1].Write(x1);
            float g = gain * out_;
            l[n]    = (look_ > 0.0f ? line_[0].Read(look_) : x0) * g;
            r[n]    = (look_ > 0.0f ? line_[1].Read(look_) : x1) * g;
        }
    }

  private:
    Ring  line_[2];
    float env_ = 0.0f, slope_ = 0.5f, threshold_ = -2.0f, in_ = 1.0f, out_ = 1.0f;
    float attack_ = 0.1f, release_ = 0.01f, look_ = 0.0f;
};

// ---------------------------------------------------------------- EQ

// Low shelf, two bells, high shelf. The coefficients cost trig, so only
// when a setting moves.
class Eq
{
  public:
    void Reset()
    {
        for(auto &side : band_)
            for(dsp::Biquad &b : side)
                b.Reset();
    }

    void Set(const float *v, const Context &c, size_t)
    {
        mix_  = v[0];
        gain_ = dsp::FastExp2(Bipolar(v[1]) * 2.0f); // +-12 dB
        bool moved = false;
        for(int i = 2; i < 12; i++)
            if(v[i] != was_[i])
            {
                moved   = true;
                was_[i] = v[i];
            }
        if(!moved)
            return;
        using T = dsp::Biquad::Type;
        auto db = [](float x) { return 36.0f * x - 18.0f; };            // +-18 dB
        auto q  = [](float x) { return 0.3f * dsp::FastExp2(5.0f * x); }; // 0.3 to 9.6
        for(auto &side : band_)
        {
            side[0].Set(T::kLowShelf, 20.0f * dsp::FastExp2(5.64f * v[2]), 0.707f, db(v[3]), c.sample_rate);
            side[1].Set(T::kPeak, 100.0f * dsp::FastExp2(6.32f * v[4]), q(v[6]), db(v[5]), c.sample_rate);
            side[2].Set(T::kPeak, 200.0f * dsp::FastExp2(5.91f * v[7]), q(v[9]), db(v[8]), c.sample_rate);
            side[3].Set(T::kHighShelf, 1000.0f * dsp::FastExp2(4.0f * v[10]), 0.707f, db(v[11]), c.sample_rate);
        }
    }

    void Process(float *l, float *r, size_t size)
    {
        float *io[2] = {l, r};
        for(int side = 0; side < 2; side++)
            for(size_t n = 0; n < size; n++)
            {
                float x = io[side][n], y = x;
                for(dsp::Biquad &b : band_[side])
                    y = b.Process(y);
                io[side][n] = dsp::Lerp(x, y * gain_, mix_);
            }
    }

  private:
    dsp::Biquad band_[2][4];
    float       was_[12] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    float       mix_ = 1.0f, gain_ = 1.0f;
};

// ---------------------------------------------------------------- Delay

// A line a side in SDRAM, 2.7 s long. The cuts are in the feedback, so each
// echo is darker (or thinner) than the last; a change of time glides, as
// tape would.
class Delay
{
  public:
    static constexpr uint32_t kLine = 131072;

    void Init(float *mem)
    {
        line_[0].Init(mem, kLine);
        line_[1].Init(mem + kLine, kLine);
    }
    void Reset()
    {
        line_[0].Reset();
        line_[1].Reset();
        lp_[0] = lp_[1] = hp_[0] = hp_[1] = 0.0f;
        delay_[0] = delay_[1] = 0.0f;
    }

    void Set(const float *v, const Context &c, size_t size)
    {
        mix_       = v[0];
        bool link  = Pick(v[3], 2) == 1;
        bool sync  = Pick(v[4], 2) == 1;
        float to[2];
        for(int side = 0; side < 2; side++)
        {
            int from = link ? 0 : side;
            to[side] = sync ? kDelayRatioBeats[Pick(v[5 + from], kNumDelayRatios)] * c.samples_per_beat
                            : dsp::FastExp2(10.97f * v[1 + from]) * 0.001f * c.sample_rate; // 1 ms to 2 s
            to[side] = dsp::Clamp(to[side], 2.0f, (float)(kLine - 4));
        }
        feedback_ = v[7];
        hc_       = OnePole(20000.0f * dsp::FastExp2(-5.32f * v[8]), c.sample_rate); // 20 kHz to 500 Hz
        lc_       = OnePole(20.0f * dsp::FastExp2(6.64f * v[9]), c.sample_rate);     // 20 Hz to 2 kHz
        float glide = OnePole(4.0f, c.block_rate);
        for(int side = 0; side < 2; side++)
        {
            float now = delay_[side] == 0.0f ? to[side] : delay_[side] + (to[side] - delay_[side]) * glide;
            if(delay_[side] == 0.0f)
                delay_[side] = now;
            step_[side] = (now - delay_[side]) / (float)size;
        }
    }

    void Process(float *l, float *r, size_t size)
    {
        float *io[2] = {l, r};
        for(int side = 0; side < 2; side++)
            for(size_t n = 0; n < size; n++)
            {
                float x = io[side][n];
                delay_[side] += step_[side];
                float echo = line_[side].Read(delay_[side] - 1.0f);
                lp_[side] += (echo - lp_[side]) * hc_;
                hp_[side] += (lp_[side] - hp_[side]) * lc_;
                float wet  = lp_[side] - hp_[side];
                line_[side].Write(x + Clip(feedback_ * wet));
                io[side][n] = dsp::Lerp(x, wet, mix_);
            }
    }

  private:
    Ring  line_[2];
    float delay_[2] = {}, step_[2] = {}, lp_[2] = {}, hp_[2] = {};
    float mix_ = 0.3f, feedback_ = 0.4f, hc_ = 1.0f, lc_ = 0.0f;
};

// ---------------------------------------------------------------- Reverb

// The shared Plate (common/plate.h), fed stereo. Color below the middle
// darkens the tank; above it, thins the output with a high-pass.
class Reverb
{
  public:
    void Init(float *mem) { plate_.Init(mem, nullptr, true); }

    // Empties the tank a little each block; true once it's clear.
    bool Clear() { return plate_.Clear(4096); }

    void Set(const float *v, const Context &c, size_t size)
    {
        mix_       = v[0];
        float rt60 = 0.3f * dsp::FastExp2(6.0f * v[1]); // 0.3 to 19 s
        plate_.SetDecay(fminf(plate_.DecayForRt60(rt60 * c.sample_rate), 0.98f));
        float color = Bipolar(v[2]);
        plate_.SetDamping(color < 0.0f ? 0.2f - 0.7f * color : 0.2f * (1.0f - color));
        plate_.SetBandwidth(color < 0.0f ? 1.0f + 0.5f * color : 1.0f);
        hp_coef_ = color > 0.0f ? OnePole(20.0f * dsp::FastExp2(5.3f * color), c.sample_rate) : 0.0f;
        plate_.SetDiffusion(1.0f);
        plate_.SetInputSide(1.0f);
        plate_.SetExcursion(60.0f * v[3]);
        plate_.SetPredelay(v[5] * Plate::kMaxPredelay);
        phase_ += 0.1f * dsp::FastExp2(6.0f * v[4]) / c.block_rate; // 0.1 to 6.4 Hz
        phase_ -= floorf(phase_);
        float to[2] = {dsp::FastSin(phase_), dsp::FastSin(phase_ + 0.25f < 1.0f ? phase_ + 0.25f : phase_ - 0.75f)};
        for(int i = 0; i < 2; i++)
            lfo_step_[i] = (to[i] - lfo_[i]) / (float)size;
    }

    void Process(float *l, float *r, size_t size)
    {
        for(size_t n = 0; n < size; n++)
        {
            lfo_[0] += lfo_step_[0];
            lfo_[1] += lfo_step_[1];
            float wl, wr;
            plate_.Process(0.5f * (l[n] + r[n]), 0.5f * (l[n] - r[n]), lfo_[0], lfo_[1], &wl, &wr);
            wl *= kLevel;
            wr *= kLevel;
            hp_[0] += (wl - hp_[0]) * hp_coef_;
            hp_[1] += (wr - hp_[1]) * hp_coef_;
            l[n] = dsp::Lerp(l[n], wl - hp_[0], mix_);
            r[n] = dsp::Lerp(r[n], wr - hp_[1], mix_);
        }
    }

  private:
    static constexpr float kLevel = 1.6f; // the plate's 0.6 to about unity

    Plate plate_;
    float mix_ = 0.25f, phase_ = 0.0f, lfo_[2] = {}, lfo_step_[2] = {};
    float hp_coef_ = 0.0f, hp_[2] = {};
};
} // namespace fx

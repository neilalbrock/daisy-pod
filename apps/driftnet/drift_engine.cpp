#include "drift_engine.h"

#include "dsp.h"

namespace
{
constexpr float kFadeSeconds = 0.005f; // the Wet Channel on and off, and each half of a mode change
}

void DriftEngine::Init(float sample_rate, const Buffers &buffers)
{
    clock_.Init(sample_rate);
    looper_.Init(buffers.loop, buffers.loop_frames);
    delay_.Init(buffers.delay, buffers.delay_frames);
    slip_.Init(buffers.slip, buffers.slip_frames);
    reverb_.Init(buffers.reverb, buffers.reverb_frames);
    SetParams(params_);
}

void DriftEngine::SetParams(const Params &p)
{
    params_ = p;

    clock_.SetKnob(p.clock);
    float rate = clock_.Rate();
    wet_inc_   = 1.0f / (kFadeSeconds * rate);

    looper_.SetRate(rate);
    looper_.SetMode(p.loop_mode);
    looper_.SetLength(p.length);
    looper_.SetModify(p.loop_modify);

    delay_.SetRate(rate);
    delay_.SetTime(p.time);
    delay_.SetFeedback(p.wet_modify);
    delay_.SetFreeze(p.freeze);

    reverb_.SetRate(rate);
    reverb_.SetTime(p.time);
    reverb_.SetModify(p.wet_modify);
    reverb_.SetFreeze(p.freeze);

    slip_.SetTime(p.time);
    slip_.SetModify(p.wet_modify);
    slip_.SetFreeze(p.freeze);

    // Both at full at noon: Mix turns one down, never both up.
    float mix = dsp::Clamp(p.mix, 0.0f, 1.0f);
    dry_      = fminf(1.0f, 2.0f * (1.0f - mix));
    wet_      = fminf(1.0f, 2.0f * mix);
}

void DriftEngine::ProcessWet(float in_l, float in_r, float *out_l, float *out_r)
{
    // A mode change fades out, resets the new mode, and fades back in.
    if(params_.wet_mode != wet_mode_)
    {
        mode_gain_ -= wet_inc_;
        if(mode_gain_ <= 0.0f)
        {
            mode_gain_ = 0.0f;
            wet_mode_  = params_.wet_mode;
            switch(wet_mode_)
            {
                case WetMode::kReverb: reverb_.Reset(); break;
                case WetMode::kDelay: delay_.Reset(); break;
                case WetMode::kSlip: slip_.Reset(); break;
            }
        }
    }
    else if(mode_gain_ < 1.0f)
        mode_gain_ = fminf(mode_gain_ + wet_inc_, 1.0f);

    // The input fades too, so a fresh mode's first echoes fade in.
    in_l *= mode_gain_;
    in_r *= mode_gain_;
    switch(wet_mode_)
    {
        case WetMode::kReverb: reverb_.Process(in_l, in_r, out_l, out_r); break;
        case WetMode::kDelay: delay_.Process(in_l, in_r, out_l, out_r); break;
        case WetMode::kSlip: slip_.Process(in_l, in_r, out_l, out_r); break;
    }
    *out_l *= mode_gain_;
    *out_r *= mode_gain_;
}

void DriftEngine::Tick(float in_l, float in_r, float *out_l, float *out_r)
{
    float loop_l, loop_r;
    looper_.Sense(in_l, in_r);
    looper_.Read(&loop_l, &loop_r);
    float loop_gain = looper_.Gain(); // the loop's fade, already in loop_l/r

    float target = params_.wet_on ? 1.0f : 0.0f;
    if(wet_gain_ < target)
        wet_gain_ = fminf(wet_gain_ + wet_inc_, target);
    else if(wet_gain_ > target)
        wet_gain_ = fmaxf(wet_gain_ - wet_inc_, target);

    // What the Wet Channel hears. Loop-only hands over from the input as
    // the loop fades in.
    float send_l = in_l, send_r = in_r;
    switch(params_.routing)
    {
        case Routing::kIn: break;
        case Routing::kInLoop:
            send_l += loop_l;
            send_r += loop_r;
            break;
        case Routing::kLoop:
            send_l = in_l * (1.0f - loop_gain) + loop_l;
            send_r = in_r * (1.0f - loop_gain) + loop_r;
            break;
    }

    float wet_l, wet_r;
    ProcessWet(send_l * wet_gain_, send_r * wet_gain_, &wet_l, &wet_r);
    wet_l *= wet_gain_;
    wet_r *= wet_gain_;

    // Listening, the looper catches the Wet Channel too; overdubs take only
    // the clean input, or the loop would feed itself through the effect.
    if(looper_.Playing())
        looper_.Write(in_l, in_r);
    else
        looper_.Write(in_l + wet_l, in_r + wet_r);

    // A loop sent through the Wet Channel is heard only through it.
    float direct = params_.routing == Routing::kIn ? 1.0f : 1.0f - wet_gain_;
    *out_l       = wet_l + loop_l * direct;
    *out_r       = wet_r + loop_r * direct;
}

void DriftEngine::Process(float in_l, float in_r, float *out_l, float *out_r)
{
    float l, r;
    clock_.Begin(in_l, in_r);
    float tick_l, tick_r;
    while(clock_.NextTick(&tick_l, &tick_r))
    {
        Tick(tick_l, tick_r, &l, &r);
        clock_.Push(l, r);
    }
    clock_.Output(&l, &r);
    *out_l = in_l * dry_ + l * wet_;
    *out_r = in_r * dry_ + r * wet_;
}

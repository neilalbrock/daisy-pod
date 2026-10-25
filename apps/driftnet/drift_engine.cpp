#include "drift_engine.h"

#include "dsp.h"

namespace
{
constexpr float kFadeSeconds = 0.005f; // the Wet Channel switching on and off
}

void DriftEngine::Init(float  sample_rate,
                       float *loop_data,
                       size_t loop_frames,
                       float *delay_data,
                       size_t delay_frames)
{
    clock_.Init(sample_rate);
    looper_.Init(loop_data, loop_frames);
    delay_.Init(delay_data, delay_frames);
    SetParams(params_);
}

void DriftEngine::SetParams(const Params &p)
{
    params_ = p;

    clock_.SetKnob(p.clock);
    float rate = clock_.Rate();
    wet_inc_   = 1.0f / (kFadeSeconds * rate);

    looper_.SetRate(rate);
    looper_.SetLength(p.length);
    looper_.SetModify(p.loop_modify);

    delay_.SetRate(rate);
    delay_.SetTime(p.time);
    delay_.SetFeedback(p.wet_modify);
    delay_.SetFreeze(p.freeze);

    // Both at full at noon: Mix turns one down, never both up.
    float mix = dsp::Clamp(p.mix, 0.0f, 1.0f);
    dry_      = fminf(1.0f, 2.0f * (1.0f - mix));
    wet_      = fminf(1.0f, 2.0f * mix);
}

void DriftEngine::Tick(float in_l, float in_r, float *out_l, float *out_r)
{
    float loop_l, loop_r;
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
    delay_.Process(send_l * wet_gain_, send_r * wet_gain_, &wet_l, &wet_r);
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

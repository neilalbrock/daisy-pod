#include "ramp.h"

#include <cmath>

namespace
{
// RAMP SPEED: a bounce's cycles a second (and a ramp's 1 / duration), from
// one every 50 s to 10 a second.
constexpr float kMinHz = 0.02f, kMaxHz = 10.0f;
} // namespace

void Ramp::Init(float block_rate)
{
    block_rate_ = block_rate;
}

bool Ramp::Active() const
{
    for(bool t : settings_.targets)
        if(t)
            return true;
    return false;
}

float Ramp::Wave() const
{
    // The five shapes, 0..1, all low at the start of the cycle and high in
    // the middle, so the morph between neighbours stays smooth.
    float p        = phase_;
    float shapes[5];
    shapes[0] = 1.0f - fabsf(2.0f * p - 1.0f);                       // triangle
    shapes[1] = p >= 0.25f && p < 0.75f ? 1.0f : 0.0f;               // square
    shapes[2] = 0.5f + 0.5f * dsp::FastSin(dsp::WrapPhase(p + 0.75f)); // sine
    shapes[3] = rand_next_;                                           // random
    float s   = p * p * (3.0f - 2.0f * p);
    shapes[4] = rand_prev_ + s * (rand_next_ - rand_prev_); // smoothed random

    float x = dsp::Clamp(settings_.shape, 0.0f, 1.0f) * 4.0f;
    int   i = x >= 4.0f ? 3 : (int)x;
    return shapes[i] + (x - (float)i) * (shapes[i + 1] - shapes[i]);
}

void Ramp::Update(const Settings &s)
{
    settings_ = s;
    if(!s.running)
        return;

    if(s.speed != speed_set_)
    {
        speed_set_ = s.speed;
        inc_ = kMinHz * powf(kMaxHz / kMinHz, dsp::Clamp(s.speed, 0.0f, 1.0f)) / block_rate_;
    }
    float inc = inc_;
    ramp_     = fminf(1.0f, ramp_ + inc);

    phase_ += inc;
    if(phase_ >= 1.0f)
    {
        phase_ -= 1.0f;
        rand_prev_ = rand_next_;
        rand_next_ = rng_.Uni();
    }
    wave_ = Wave();
}

float Ramp::Apply(Target t, float knob) const
{
    const Settings &s = settings_;
    if(!s.targets[t])
        return knob;

    // The range: from the bottom to the knob, or from the knob to the top.
    float lo = s.top ? knob : 0.0f;
    float hi = s.top ? 1.0f : knob;

    float x;
    if(s.expression)
        x = s.reverse ? 1.0f - s.pedal : s.pedal;
    else if(s.bounce)
        x = s.reverse ? 1.0f - wave_ : wave_;
    else
    {
        // A ramp ends on the knob: rising from the bottom, or falling from
        // the top. POLARITY starts it on the knob instead.
        bool rising = s.top == s.reverse;
        x           = rising ? ramp_ : 1.0f - ramp_;
    }
    return lo + x * (hi - lo);
}

float Ramp::ApplyMix(float mix) const
{
    if(!settings_.expression || Active())
        return mix;
    return settings_.reverse ? 1.0f - settings_.pedal : settings_.pedal;
}

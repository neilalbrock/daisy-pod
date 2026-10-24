#include "tape_engine.h"

using namespace dsp;

namespace
{
constexpr float kWowMaxS     = 0.012f;   // wow depth at full, seconds of delay
constexpr float kFlutterMaxS = 0.00035f; // flutter depth at full
constexpr float kStopS       = 1.0f;     // AUX stop: time to wind down
constexpr float kStartS      = 0.5f;     // ... and to spin back up
constexpr float kFailS       = 0.3f;     // AUX fail onset
constexpr float kFilterS     = 0.15f;    // AUX filter onset
constexpr float kCatchupS    = 0.05f;    // crossfade back to real time after a stop
constexpr float kHumHz       = 50.0f;

float Frac(float x)
{
    return x - (float)(int)x;
}

// Moves x towards 1 when `on`, towards 0 otherwise, by inc per call.
float Ramp(float x, bool on, float inc_up, float inc_down)
{
    return on ? (x + inc_up > 1.0f ? 1.0f : x + inc_up)
              : (x - inc_down < 0.0f ? 0.0f : x - inc_down);
}
} // namespace

void TapeEngine::Init(float  sample_rate,
                      float *buf_l,
                      float *buf_r,
                      size_t buffer_len)
{
    sample_rate_ = sample_rate;
    len_         = buffer_len;
    mask_        = buffer_len - 1;
    write_       = 0;
    for(size_t i = 0; i < len_; i++)
    {
        buf_l[i] = 0.0f;
        buf_r[i] = 0.0f;
    }

    ch_[0].buf = buf_l;
    ch_[1].buf = buf_r;
    for(int c = 0; c < 2; c++)
    {
        Channel &ch = ch_[c];
        uint32_t seed = 0x1234567u + 0x9E3779B9u * (uint32_t)c;
        ch.hysteresis.Init(sample_rate);
        ch.wow.Init(sample_rate, seed ^ 0x51u);
        ch.flutter.Init(sample_rate, seed ^ 0xF1u);
        ch.failure.Init(sample_rate, seed ^ 0xFA11u);
        ch.model.Init(sample_rate);
        ch.hiss_rng.Seed(seed ^ 0x415u);
    }

    depth_coef_  = SmoothCoef(0.05f, sample_rate);
    centre_coef_ = SmoothCoef(0.5f, sample_rate);
    stop_inc_    = 1.0f / (kStopS * sample_rate);
    start_inc_   = 1.0f / (kStartS * sample_rate);
    fail_inc_    = 1.0f / (kFailS * sample_rate);
    filter_inc_  = 1.0f / (kFilterS * sample_rate);
    catchup_inc_ = 1.0f / (kCatchupS * sample_rate);
    hum_inc_     = kHumHz / sample_rate;

    // Leave room for the deepest wow, flutter and snags on top.
    max_excess_ = (float)len_ - 0.03f * sample_rate;

    SetParams(Params());
    wow_depth_     = wow_depth_target_;
    flutter_depth_ = flutter_depth_target_;
    centre_        = centre_target_;
}

void TapeEngine::SetParams(const Params &p)
{
    params_          = p;
    params_.wow      = Clamp(p.wow, 0.0f, 1.0f);
    params_.flutter  = Clamp(p.flutter, 0.0f, 1.0f);
    params_.saturate = Clamp(p.saturate, 0.0f, 1.0f);
    params_.failure  = Clamp(p.failure, 0.0f, 1.0f);
    params_.noise    = Clamp(p.noise, 0.0f, 1.0f);
    params_.spread   = Clamp(p.spread, 0.0f, 1.0f);

    // Squared so the first half of each knob stays subtle.
    float w = params_.wow, f = params_.flutter, n = params_.noise;
    wow_depth_target_     = kWowMaxS * sample_rate_ * w * w;
    flutter_depth_target_ = kFlutterMaxS * sample_rate_ * f * f;
    flutter_am_           = 0.2f * f;

    // The delay sits just far enough back for the modulation to swing
    // either side of it, so with everything off the wet path is only ~1 ms
    // late and blends with the dry signal without comb filtering.
    centre_target_ = 0.001f * sample_rate_
                     + 1.05f * (wow_depth_target_ + flutter_depth_target_);

    spread_     = params_.spread;
    hiss_level_ = 0.03f * n * n;
    hum_level_  = 0.004f * n * n;

    for(auto &ch : ch_)
    {
        ch.hysteresis.SetAmount(params_.saturate);
        ch.model.Select(params_.model);
        ch.failure.SetAmount(params_.failure, fail_amt_);
    }
}

float TapeEngine::Read(const float *buf, float delay) const
{
    delay = Clamp(delay, 2.0f, (float)len_ - 4.0f);

    // Split before subtracting from the write index so the fraction keeps
    // full float precision however far the tape stop has drifted.
    size_t whole = (size_t)delay;
    float  t     = 1.0f - (delay - (float)whole); // 0 < t <= 1 past sample i
    size_t i     = (write_ + len_ - whole - 1) & mask_;

    float xm1 = buf[(i - 1) & mask_];
    float x0  = buf[i];
    float x1  = buf[(i + 1) & mask_];
    float x2  = buf[(i + 2) & mask_];

    // 4-point, 3rd-order Hermite
    float c1 = 0.5f * (x1 - xm1);
    float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

void TapeEngine::Process(float in_l, float in_r, float *out_l, float *out_r)
{
    // AUX ramps. Each effect eases in and out on its own, so switching the
    // AUX type while it's engaged crossfades from one to the other.
    bool on    = params_.aux_on;
    stop_pos_  = Ramp(stop_pos_, on && params_.aux == Aux::kStop, stop_inc_, start_inc_);
    fail_amt_  = Ramp(fail_amt_, on && params_.aux == Aux::kFail, fail_inc_, fail_inc_);
    filter_amt_ = Ramp(filter_amt_, on && params_.aux == Aux::kFilter, filter_inc_, filter_inc_);

    // Tape speed eases off slowly then gives out; spinning up, it catches
    // fast then settles. The wet signal fades as the pitch bottoms out.
    float speed     = 1.0f - stop_pos_ * stop_pos_;
    float stop_gain = Clamp(speed * 2.5f, 0.0f, 1.0f);
    stop_gain *= stop_gain;

    if(stop_pos_ >= 1.0f)
        excess_ = 0.0f; // silent while stopped, so jump back to real time
    else if(excess_ + 1.0f - speed < max_excess_)
        excess_ += 1.0f - speed;
    if(stop_pos_ <= 0.0f && excess_ > 0.0f && catchup_ >= 1.0f)
    {
        catchup_excess_ = excess_;
        excess_         = 0.0f;
        catchup_        = 0.0f;
    }

    wow_depth_ += depth_coef_ * (wow_depth_target_ - wow_depth_);
    flutter_depth_ += depth_coef_ * (flutter_depth_target_ - flutter_depth_);
    centre_ += centre_coef_ * (centre_target_ - centre_);

    // Modulation. The right channel blends from following the left (spread
    // 0) to its own independent generators (spread 1).
    float wow[2], flutter[2];
    wow[0]     = ch_[0].wow.Process();
    wow[1]     = Lerp(wow[0], ch_[1].wow.Process(), spread_);
    flutter[0] = ch_[0].flutter.Process();
    flutter[1] = Lerp(flutter[0], ch_[1].flutter.Process(), spread_);

    TapeFailure::Out fail[2];
    fail[0]       = ch_[0].failure.Process();
    fail[1]       = ch_[1].failure.Process();
    fail[1].gain  = Lerp(fail[0].gain, fail[1].gain, spread_);
    fail[1].delay = Lerp(fail[0].delay, fail[1].delay, spread_);
    fail[1].sound = Lerp(fail[0].sound, fail[1].sound, spread_);

    // Mains hum with a little buzz, from the motor and transformer.
    hum_phase_ = WrapPhase(hum_phase_ + hum_inc_);
    float hum  = FastSin(hum_phase_) + 0.5f * FastSin(Frac(hum_phase_ * 2.0f))
                + 0.3f * FastSin(Frac(hum_phase_ * 3.0f))
                + 0.15f * FastSin(Frac(hum_phase_ * 5.0f));
    hum *= hum_level_;

    const float in[2] = {in_l, in_r};
    float       out[2];
    for(int c = 0; c < 2; c++)
    {
        Channel &ch = ch_[c];

        // Record
        ch.buf[write_] = ch.dc.Process(ch.hysteresis.Process(in[c]));

        // Play back
        float delay = centre_ + wow_depth_ * wow[c] + flutter_depth_ * flutter[c]
                      + fail[c].delay + excess_;
        float y = Read(ch.buf, delay);
        if(catchup_ < 1.0f)
            y = Lerp(Read(ch.buf, delay + catchup_excess_), y, catchup_);
        y *= 1.0f - flutter_am_ * (0.5f + 0.5f * flutter[c]);

        y += hiss_level_ * ch.hiss_rng.Bi() + fail[c].sound;
        y = Lerp(ch.model.Process(y), y, filter_amt_);
        y *= fail[c].gain * stop_gain;
        out[c] = y + hum;
    }

    if(catchup_ < 1.0f)
        catchup_ += catchup_inc_;
    write_ = (write_ + 1) & mask_;

    *out_l = out[0];
    *out_r = out[1];
}

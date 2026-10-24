#include "tape_model.h"

using namespace dsp;

// Imaginary machines, roughly in order of decay. gain_db is a partial level
// match measured offline with pink noise, so narrow models still sound
// smaller, but not much quieter.
// clang-format off
const TapeModel::Profile TapeModel::kProfiles[kNumModels] = {
    // name              hp      lp      lp_q  peak    pk_q  pk_db  gain_db
    {"Flat",             0,      0,      0.0f, 0,      0.0f,  0.0f, 0.0f},
    {"Reel-to-reel",     25,     16000,  0.7f, 55,     1.3f,  4.0f, 0.0f},
    {"Cassette deck",    30,     12500,  0.7f, 70,     1.0f,  2.0f, 0.5f},
    {"Walkman",          50,     10000,  0.8f, 900,    0.7f, -2.5f, 1.5f},
    {"VHS hi-fi",        40,     9000,   0.7f, 3000,   1.0f,  1.5f, 1.0f},
    {"VHS 2nd gen",      55,     6500,   0.8f, 180,    0.8f,  2.0f, 1.0f},
    {"VHS 3rd gen",      80,     4200,   0.9f, 1200,   0.7f,  2.0f, 1.5f},
    {"4-track half speed", 45,   5500,   1.0f, 110,    1.0f,  3.0f, 0.5f},
    {"Camcorder",        180,    7000,   0.9f, 2200,   1.2f,  4.0f, 1.5f},
    {"Dictaphone",       350,    3800,   1.1f, 1600,   1.4f,  5.0f, 2.0f},
    {"Answering machine", 300,   3200,   0.8f, 900,    1.0f,  3.0f, 2.5f},
    {"Toy recorder",     600,    2600,   1.5f, 1100,   2.0f,  6.0f, 2.5f},
    {"Melted",           120,    1800,   2.5f, 600,    1.5f,  4.0f, 1.0f},
};
// clang-format on

namespace
{
constexpr float kFadeSeconds = 0.03f;
}

const char *TapeModel::Name(int model)
{
    return kProfiles[(int)Clamp((float)model, 0.0f, kNumModels - 1.0f)].name;
}

void TapeModel::Chain::Set(const Profile &p, float sample_rate)
{
    using T = Biquad::Type;
    off = p.hp_hz <= 0.0f && p.lp_hz <= 0.0f && p.peak_hz <= 0.0f;
    if(p.hp_hz > 0.0f)
        hp.Set(T::kHighPass, p.hp_hz, 0.707f, 0.0f, sample_rate);
    else
        hp.SetUnity();
    if(p.lp_hz > 0.0f)
    {
        // A resonant stage, then a gentler one a little higher for slope.
        lp1.Set(T::kLowPass, p.lp_hz, p.lp_q, 0.0f, sample_rate);
        lp2.Set(T::kLowPass, p.lp_hz * 1.3f, 0.6f, 0.0f, sample_rate);
    }
    else
    {
        lp1.SetUnity();
        lp2.SetUnity();
    }
    if(p.peak_hz > 0.0f)
        peak.Set(T::kPeak, p.peak_hz, p.peak_q, p.peak_db, sample_rate);
    else
        peak.SetUnity();
    gain = DbToGain(p.gain_db);

    hp.Reset();
    lp1.Reset();
    lp2.Reset();
    peak.Reset();
}

float TapeModel::Chain::Process(float x)
{
    if(off)
        return x;
    return peak.Process(lp2.Process(lp1.Process(hp.Process(x)))) * gain;
}

void TapeModel::Init(float sample_rate)
{
    sample_rate_ = sample_rate;
    fade_inc_    = 1.0f / (kFadeSeconds * sample_rate);
    current_     = 0;
    model_ = pending_ = 0;
    fade_             = 1.0f;
    chains_[0].Set(kProfiles[0], sample_rate);
    chains_[1].Set(kProfiles[0], sample_rate);
}

void TapeModel::Select(int model)
{
    pending_ = (int)Clamp((float)model, 0.0f, kNumModels - 1.0f);
}

float TapeModel::Process(float x)
{
    if(fade_ >= 1.0f && pending_ != model_)
    {
        current_ ^= 1;
        model_ = pending_;
        chains_[current_].Set(kProfiles[model_], sample_rate_);
        fade_ = 0.0f;
    }

    float y = chains_[current_].Process(x);
    if(fade_ < 1.0f)
    {
        float old = chains_[current_ ^ 1].Process(x);
        y         = Lerp(old, y, fade_);
        fade_ += fade_inc_;
    }
    return y;
}

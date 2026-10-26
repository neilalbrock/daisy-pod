#include "space.h"

#include <cmath>

#include "dsp.h"

using namespace dsp;

namespace
{
constexpr float kRate         = 48000.0f;
constexpr float kExcursion    = 16.0f * kRate / 29761.0f; // the paper's swing, at 48 kHz
constexpr float kLfoHz        = 0.7f;
constexpr float kFadeStep     = 1.0f / (0.01f * kRate); // 10 ms each way
constexpr uint32_t kClearStep = 512; // floats emptied a sample for a mode change

struct ModeDef
{
    float size;      // the tank's lengths, as a fraction of the paper's
    float rt60;      // seconds
    float damping;   // in the tank: 0 bright .. 1 dark
    float bandwidth; // at the input: 1 open .. 0 dark
    float predelay;  // seconds
    float mod;       // modulation, as a fraction of the paper's
    float gain;      // output level
};
constexpr ModeDef kModes[Space::kNumModes] = {
    {0.45f, 0.8f, 0.1f, 0.9995f, 0.004f, 0.3f, 0.6f}, // A Bright Room
    {0.65f, 1.8f, 0.6f, 0.45f, 0.012f, 0.5f, 0.8f},   // B Dark Medium
    {0.9f, 3.5f, 0.35f, 0.75f, 0.025f, 0.6f, 0.65f},  // C Large Hall
    {1.0f, 9.0f, 0.45f, 0.65f, 0.06f, 1.0f, 0.6f},    // D Ambient
};
} // namespace

void Space::Init(float *mem)
{
    plate_.Init(mem, nullptr, false);
    mode_ = -1;
    fade_ = 0.0f;
}

void Space::SetMix(float space)
{
    mix_target_ = Clamp(space, 0.0f, 1.0f);
}

void Space::SetMode(int mode)
{
    target_mode_ = mode < 0 ? 0 : (mode >= kNumModes ? kNumModes - 1 : mode);
}

void Space::Apply(int mode)
{
    const ModeDef &m = kModes[mode];
    plate_.SetSize(m.size);
    plate_.SetPredelay(m.predelay * kRate);
    plate_.SetDecay(fminf(plate_.DecayForRt60(m.rt60 * kRate), 0.98f));
    plate_.SetDamping(m.damping);
    plate_.SetBandwidth(m.bandwidth);
    plate_.SetExcursion(kExcursion * m.mod);
    gain_ = m.gain;
    mode_ = mode;
}

void Space::Process(float *l, float *r)
{
    mix_ += 0.001f * (mix_target_ - mix_); // about 20 ms
    float dry_gain = FastSin(0.25f + 0.25f * mix_); // cos(mix * pi / 2)
    float wet_gain = FastSin(0.25f * mix_);         // sin(mix * pi / 2)

    // A mode change: fade out, empty the lines a step at a time while only
    // the dry is heard, switch size and fade back in.
    if(target_mode_ != mode_)
    {
        if(fade_ > 0.0f)
            fade_ = fmaxf(fade_ - kFadeStep, 0.0f);
        else
        {
            if(plate_.Clear(kClearStep))
                Apply(target_mode_);
            *l *= dry_gain;
            *r *= dry_gain;
            return;
        }
    }
    else if(fade_ < 1.0f)
        fade_ = fminf(fade_ + kFadeStep, 1.0f);

    lfo_phase_ = WrapPhase(lfo_phase_ + kLfoHz / kRate);
    float wet_l, wet_r;
    plate_.Process(0.5f * (*l + *r), 0.0f, FastSin(lfo_phase_),
                   FastSin(WrapPhase(lfo_phase_ + 0.25f)), &wet_l, &wet_r);

    float wet = gain_ * fade_ * wet_gain;
    *l        = *l * dry_gain + wet_l * wet;
    *r        = *r * dry_gain + wet_r * wet;
}

#include "slow_engine.h"

#include <cmath>

using namespace dsp;

namespace
{
// Each mode's predelay, the plate's damping at the Filter's ends, the
// low-pass after it at the Filter's ends, and its level.
struct ModeDef
{
    float predelay_ms;
    float damp_dark, damp_bright; // in the tank: 0 bright .. 1 dark
    float lp_dark, lp_bright;     // Hz
    float gain;
};
constexpr ModeDef kModes[SlowEngine::kNumModes] = {
    {20.0f, 0.6f, 0.1f, 500.0f, 12000.0f, 1.0f},  // Dream
    {10.0f, 0.4f, 0.0f, 1200.0f, 18000.0f, 0.9f}, // Light
    {45.0f, 0.5f, 0.1f, 800.0f, 12000.0f, 1.0f},  // Rain
    {25.0f, 0.7f, 0.3f, 300.0f, 7000.0f, 1.1f},   // Dark
    {15.0f, 0.6f, 0.1f, 600.0f, 14000.0f, 1.0f},  // Rise
};

constexpr float kMinStretchRate = 12000.0f; // two octaves down
constexpr float kMinRt60        = 0.3f;     // s
constexpr float kMaxRt60        = 20.0f;
constexpr float kSustainRt60    = 60.0f;
constexpr float kFreezeRt60     = 1e6f;  // as good as forever
constexpr float kCutRt60        = 0.05f; // off without trails: let the tail die unheard
constexpr float kMaxShimmer     = 0.8f;  // the share of the tank shifted at full X
constexpr float kMaxDark        = 1.2f;  // the octave down's level at full X
constexpr float kMaxVibrato     = 192.0f; // frames of swing at full X
constexpr float kOutSide[SlowEngine::kNumWidths] = {0.0f, 0.6f, 1.0f};
constexpr float kInSide[SlowEngine::kNumWidths]  = {0.0f, 0.0f, 1.0f};

// The one-pole coefficient per block for a time constant.
float BlockCoef(float seconds, float block_rate)
{
    return 1.0f - expf(-1.0f / (seconds * block_rate));
}
} // namespace

void SlowEngine::Init(float sample_rate, size_t block_size, float *mem)
{
    sample_rate_ = sample_rate;
    block_rate_  = sample_rate / (float)block_size;

    clock_.Init(sample_rate);
    rate_ = sample_rate;
    clock_.SetHz(rate_);

    plate_.Init(mem, mem + Plate::kFloats);
    mem += Plate::kFloats + PitchShift::kFloats;
    down_.Init(mem, 0.5f, 1600.0f);
    mem += PitchShift::kFloats;
    for(int c = 0; c < 2; c++)
    {
        vib_[c] = mem;
        for(size_t i = 0; i < kVibratoFloats; i++)
            vib_[c][i] = 0.0f;
        mem += kVibratoFloats;
    }

    onset_.Init(sample_rate);
    swell_.Init(sample_rate);

    coef_.fast      = BlockCoef(0.05f, block_rate_);
    coef_.slow      = BlockCoef(0.2f, block_rate_);
    coef_.rate      = BlockCoef(0.08f, block_rate_);
    coef_.diffusion = BlockCoef(0.1f, block_rate_);
    coef_.cutoff    = BlockCoef(0.03f, block_rate_);
    coef_.mix       = BlockCoef(0.02f, block_rate_);
    coef_.rt_up     = BlockCoef(0.3f, block_rate_);
    coef_.rt_down   = BlockCoef(1.0f, block_rate_);
    coef_.rt_cut    = BlockCoef(0.05f, block_rate_);

    log_rt60_  = logf(2.0f);
    predelay_  = kModes[kDream].predelay_ms * 0.001f * sample_rate;
    gate_step_ = 1.0f / (0.02f * sample_rate);
    fade_step_ = 1.0f / (0.005f * sample_rate);
    SetParams(Params());
}

void SlowEngine::SetParams(const Params &p)
{
    mode_            = p.mode < 0 ? 0 : (p.mode >= kNumModes ? kNumModes - 1 : p.mode);
    const ModeDef &m = kModes[mode_];
    int width        = p.width < 0 ? 0 : (p.width >= kNumWidths ? kNumWidths - 1 : p.width);
    float x          = Clamp(p.x, 0.0f, 1.0f);
    float filter     = Clamp(p.filter, 0.0f, 1.0f);

    float fast = coef_.fast;

    // Stretch: down two octaves, gliding so the knob doesn't jitter the
    // pitch of the tail.
    float rate_target = sample_rate_ * exp2f(-2.0f * Clamp(p.stretch, 0.0f, 1.0f));
    rate_ += coef_.rate * (rate_target - rate_);
    rate_ = fmaxf(rate_, kMinStretchRate);
    clock_.SetHz(rate_);

    lfo_.SetShape(p.shape);
    lfo_.Set(0.05f * powf(200.0f, Clamp(p.rate, 0.0f, 1.0f)), rate_);
    // Dream's vibrato has its own LFO, in vibrato's range, on the same shape.
    vib_lfo_.SetShape(p.shape);
    vib_lfo_.Set(2.0f + 6.0f * Clamp(p.rate, 0.0f, 1.0f), rate_);

    // Decay, eased in log time: Sustain and Freeze come in over about
    // 0.3 s and let go over about a second. Off without trails, the tail is
    // cut, so it dies away fast, unheard, and doesn't come back when the
    // effect does.
    bool  cut  = !p.active && !p.trails;
    float rt60 = kMinRt60 * powf(kMaxRt60 / kMinRt60, Clamp(p.decay, 0.0f, 1.0f));
    if(p.sustain)
        rt60 = kSustainRt60;
    if(p.freeze)
        rt60 = kFreezeRt60;
    if(cut)
        rt60 = kCutRt60;
    float log_target = logf(rt60);
    float rt_coef    = cut ? coef_.rt_cut : (log_target > log_rt60_ ? coef_.rt_up : coef_.rt_down);
    log_rt60_ += rt_coef * (log_target - log_rt60_);
    plate_.SetDecay(plate_.DecayForRt60(expf(log_rt60_) * sample_rate_));

    // A frozen pad keeps its brightness.
    float damping = p.freeze ? 0.0f : Lerp(m.damp_dark, m.damp_bright, filter);
    damping_ += fast * (damping - damping_);
    bandwidth_ += fast * (Lerp(0.3f, 0.9995f, filter) - bandwidth_);
    plate_.SetDamping(damping_);
    plate_.SetBandwidth(bandwidth_);

    // Rain: X sets the diffusion, from bare echoes to a smooth wash.
    diffusion_ += coef_.diffusion * ((mode_ == kRain ? x : 1.0f) - diffusion_);
    plate_.SetDiffusion(diffusion_);

    predelay_ += coef_.slow * (m.predelay_ms * 0.001f * sample_rate_ - predelay_);
    plate_.SetPredelay(predelay_);

    float depth = Clamp(p.depth, 0.0f, 1.0f);
    excursion_ += fast * (depth * depth * Plate::kMaxExcursion - excursion_);
    plate_.SetExcursion(excursion_);

    // X's other jobs.
    dark_ += fast * ((mode_ == kDark ? kMaxDark * x : 0.0f) - dark_);
    shimmer_ += fast * ((mode_ == kLight ? kMaxShimmer * x : 0.0f) - shimmer_);
    plate_.SetShimmer(shimmer_);
    vib_depth_ += fast * ((mode_ == kDream ? kMaxVibrato * x * x : 0.0f) - vib_depth_);
    swell_.SetTime(0.1f * powf(40.0f, x)); // Rise: 0.1 s to 4 s

    out_side_ += fast * (kOutSide[width] - out_side_);
    in_side_ += fast * (kInSide[width] - in_side_);
    plate_.SetInputSide(in_side_);
    // Narrowing drops the side, and the level with it: make it up.
    width_gain_ = 1.0f / sqrtf(0.5f * (1.0f + out_side_ * out_side_));

    // The mode's low-pass, at the core rate: recalculated when either the
    // cutoff or the rate moves.
    float log_cutoff = logf(m.lp_dark) + filter * logf(m.lp_bright / m.lp_dark);
    log_cutoff_ += coef_.cutoff * (log_cutoff - log_cutoff_);
    float cutoff = expf(log_cutoff_);
    if(fabsf(cutoff - cutoff_set_) > 0.002f * cutoff_set_ || rate_ != rate_set_)
    {
        lp_[0].Set(cutoff, 0.707f, rate_);
        lp_[1].CopyCoefs(lp_[0]);
        cutoff_set_ = cutoff;
        rate_set_   = rate_;
    }

    gate_target_   = p.active && !p.freeze ? 1.0f : 0.0f;
    bypass_target_ = p.active ? 0.0f : 1.0f;
    cut_target_    = !p.active && !p.trails ? 1.0f : 0.0f;

    // Mix, equal power, and pan: the wet to one side and the dry to the
    // other, centred around noon.
    mix_ += coef_.mix * (Clamp(p.mix, 0.0f, 1.0f) - mix_);
    dry_ = FastSin(0.25f + 0.25f * mix_); // cos(mix * pi / 2)
    wet_ = FastSin(0.25f * mix_) * m.gain;
    float pan = 2.0f * Clamp(p.pan, 0.0f, 1.0f) - 1.0f;
    pan       = fabsf(pan) < 0.03f ? 0.0f : (pan - copysignf(0.03f, pan)) / 0.97f;
    float to_r = fmaxf(pan, 0.0f), to_l = fmaxf(-pan, 0.0f);
    pan_wet_[0] += fast * ((1.0f - to_r) - pan_wet_[0]);
    pan_wet_[1] += fast * ((1.0f - to_l) - pan_wet_[1]);
    pan_dry_[0] += fast * ((1.0f - to_l) - pan_dry_[0]);
    pan_dry_[1] += fast * ((1.0f - to_r) - pan_dry_[1]);
}

float SlowEngine::Vibrato(float *line, float x, float delay)
{
    line[vib_pos_] = x;
    float    d     = 1.0f + delay;
    uint32_t i     = (uint32_t)d;
    float    frac  = d - (float)i;
    float    a     = line[(vib_pos_ + 1 - i) & (kVibratoFloats - 1)];
    float    b     = line[(vib_pos_ - i) & (kVibratoFloats - 1)];
    return a + frac * (b - a);
}

void SlowEngine::Tick(float in_l, float in_r, float *out_l, float *out_r)
{
    float lfo0, lfo1;
    lfo_.Next(&lfo0, &lfo1);

    float mid  = 0.5f * (in_l + in_r);
    float side = 0.5f * (in_l - in_r);

    // Dark: the input an octave down.
    mid += dark_ * down_.Process(mid);

    float l, r;
    plate_.Process(mid, side, lfo0, lfo1, &l, &r);
    float wet_mid  = 0.5f * width_gain_ * (l + r);
    float wet_side = 0.5f * width_gain_ * (l - r);
    l              = wet_mid + out_side_ * wet_side;
    r              = wet_mid - out_side_ * wet_side;

    // Dream's vibrato: a short delay swept by its own LFO.
    float vib, unused;
    vib_lfo_.Next(&vib, &unused);
    float delay = vib_depth_ * (0.5f + 0.5f * vib);
    l           = Vibrato(vib_[0], l, delay);
    r           = Vibrato(vib_[1], r, delay);
    vib_pos_    = (vib_pos_ + 1) & (kVibratoFloats - 1);

    float bp;
    *out_l = lp_[0].Process(l, &bp);
    *out_r = lp_[1].Process(r, &bp);
}

void SlowEngine::Process(float in_l, float in_r, float *out_l, float *out_r)
{
    // The reverb's input: gated, and in Rise, swelling in after each note.
    if(onset_.Process(0.5f * (in_l + in_r)) && mode_ == kRise)
        swell_.Trigger();
    if(gate_ < gate_target_)
        gate_ = fminf(gate_ + gate_step_, gate_target_);
    else if(gate_ > gate_target_)
        gate_ = fmaxf(gate_ - gate_step_, gate_target_);
    input_ = gate_ * swell_.Process();

    clock_.Begin(in_l * input_, in_r * input_);
    float tick_l, tick_r, wet_l, wet_r;
    while(clock_.NextTick(&tick_l, &tick_r))
    {
        Tick(tick_l, tick_r, &wet_l, &wet_r);
        clock_.Push(wet_l, wet_r);
    }
    clock_.Output(&wet_l, &wet_r);
    wet_peak_ = fmaxf(wet_peak_, fmaxf(fabsf(wet_l), fabsf(wet_r)));

    // Bypassed, the dry goes to unity, and the wet either rings out
    // (trails) or is cut.
    bypass_ += bypass_ < bypass_target_ ? fade_step_ : -fade_step_;
    bypass_ = Clamp(bypass_, 0.0f, 1.0f);
    cut_ += cut_ < cut_target_ ? fade_step_ : -fade_step_;
    cut_ = Clamp(cut_, 0.0f, 1.0f);

    float dry = dry_ + bypass_ * (1.0f - dry_);
    float wet = wet_ * (1.0f - cut_);
    *out_l    = in_l * dry * pan_dry_[0] + wet_l * wet * pan_wet_[0];
    *out_r    = in_r * dry * pan_dry_[1] + wet_r * wet * pan_wet_[1];
}

SlowEngine::Probe SlowEngine::GetProbe() const
{
    Probe p;
    p.lfo       = lfo_.Output();
    p.input     = input_;
    p.rt60      = expf(log_rt60_) * sample_rate_ / rate_;
    p.core_rate = rate_;
    return p;
}

#include "storm_engine.h"

#include <cmath>

using namespace dsp;

namespace
{
constexpr float kMinRt60  = 0.5f; // s
constexpr float kMaxRt60  = 60.0f;
constexpr float kHoldRt60 = 1e6f;  // ∞ and Freeze: as good as forever
constexpr float kCutRt60  = 0.05f; // off without trails: let the tail die unheard

// Tone: the top's decay as a share of the decay, dark to bright, and the
// output low-pass that takes over while the sea is held.
constexpr float kDarkHf       = 0.1f;
constexpr float kBrightHf     = 0.7f;
constexpr float kDarkCutoff   = 1500.0f; // Hz
constexpr float kBrightCutoff = 16000.0f;
constexpr float kOpenCutoff   = 20000.0f;

constexpr float kMaxDepth = 64.0f; // frames of swing at full Depth
constexpr float kTideDb   = 4.0f;  // the level swell at full Tide, each way
constexpr float kTidePan  = 0.5f;  // how far it moves the sea to one side
constexpr float kLowCut   = 50.0f; // Hz, on the way in
constexpr float kWetGain  = 1.0f;

constexpr float kOutSide[StormEngine::kNumWidths] = {0.0f, 0.6f, 1.0f};
constexpr float kInSide[StormEngine::kNumWidths]  = {0.0f, 0.0f, 1.0f};

// The one-pole coefficient per block for a time constant.
float BlockCoef(float seconds, float block_rate)
{
    return 1.0f - expf(-1.0f / (seconds * block_rate));
}
} // namespace

float StormEngine::DecaySeconds(float decay)
{
    if(decay >= kInfinite)
        return 0.0f;
    return kMinRt60 * powf(kMaxRt60 / kMinRt60, Clamp(decay, 0.0f, 1.0f) / kInfinite);
}

void StormEngine::Init(float sample_rate, size_t block_size, float *fast_mem, float *slow_mem)
{
    sample_rate_     = sample_rate;
    float block_rate = sample_rate / (float)block_size;

    sea_.Init(sample_rate, block_size, fast_mem);
    tide_level_.Init(5.0f, 15.0f, block_rate, 0x51DE7u);
    tide_pan_.Init(6.0f, 14.0f, block_rate, 0xB0A7u);
    for(auto &f : low_cut_)
        f.Set(Biquad::Type::kHighPass, kLowCut, 0.707f, 0.0f, sample_rate);

    pre_ = slow_mem;
    for(size_t i = 0; i < kSlowFloats; i++)
        pre_[i] = 0.0f;
    pre_step_ = 1.0f / (0.03f * sample_rate);

    coef_.fast    = BlockCoef(0.05f, block_rate);
    coef_.size    = BlockCoef(0.5f, block_rate);
    coef_.hold    = BlockCoef(0.3f, block_rate);
    coef_.cutoff  = BlockCoef(0.03f, block_rate);
    coef_.mix     = BlockCoef(0.02f, block_rate);
    coef_.rt_up   = BlockCoef(0.3f, block_rate);
    coef_.rt_down = BlockCoef(1.0f, block_rate);
    coef_.rt_cut  = BlockCoef(0.05f, block_rate);

    Params p;
    log_rt60_  = logf(DecaySeconds(p.decay));
    size_      = p.size;
    gate_step_ = 1.0f / (0.02f * sample_rate);
    fade_step_ = 1.0f / (0.005f * sample_rate);
    SetParams(p);
}

void StormEngine::SetParams(const Params &p)
{
    sea_.UpdateWall(); // from the block just played

    int   width = p.width < 0 ? 0 : (p.width >= kNumWidths ? kNumWidths - 1 : p.width);
    float tone  = Clamp(p.tone, 0.0f, 1.0f);
    float fast  = coef_.fast;

    // Size glides, so moving it bends the tail's pitch rather than
    // jumping.
    size_ += coef_.size * (Clamp(p.size, 0.0f, 1.0f) - size_);
    sea_.SetSize(Sea::kMinSize * powf(1.0f / Sea::kMinSize, size_));

    // Decay, eased in log time: holding comes in over about 0.3 s and lets
    // go over about a second, so a freeze let go fades away at the Decay
    // setting. Off without trails, the tail is cut, so it dies away fast,
    // unheard, and doesn't come back when the effect does.
    bool  held = p.freeze || p.decay >= kInfinite;
    bool  cut  = !p.active && !p.trails;
    float rt60 = held ? kHoldRt60 : DecaySeconds(p.decay);
    if(cut)
        rt60 = kCutRt60;
    float log_target = logf(rt60);
    float rt_coef    = cut ? coef_.rt_cut : (log_target > log_rt60_ ? coef_.rt_up : coef_.rt_down);
    log_rt60_ += rt_coef * (log_target - log_rt60_);

    // Held, Tone moves from the loop's damping to the output's low-pass, so
    // the sea keeps the colour it had rather than darkening to a hum.
    hold_ += coef_.hold * ((held && !cut ? 1.0f : 0.0f) - hold_);
    float hf = kDarkHf * powf(kBrightHf / kDarkHf, tone);
    sea_.SetDecay(expf(log_rt60_), Lerp(hf, 1.0f, hold_));

    float tone_cutoff = logf(kDarkCutoff) + tone * logf(kBrightCutoff / kDarkCutoff);
    log_cutoff_ += coef_.cutoff * (Lerp(logf(kOpenCutoff), tone_cutoff, hold_) - log_cutoff_);
    float cutoff = expf(log_cutoff_);
    if(fabsf(cutoff - cutoff_set_) > 0.002f * cutoff_set_)
    {
        lp_[0].Set(cutoff, 0.707f, sample_rate_);
        lp_[1].CopyCoefs(lp_[0]);
        cutoff_set_ = cutoff;
    }

    float depth = Clamp(p.depth, 0.0f, 1.0f);
    depth_ += fast * (kMaxDepth * depth * sqrtf(depth) - depth_);
    sea_.SetDepth(depth_);

    // Tide: the swells wander on whatever Tide is set to, so turning it up
    // joins them where they are.
    tide_ += fast * (Clamp(p.tide, 0.0f, 1.0f) - tide_);
    tide_db_   = kTideDb * tide_ * tide_level_.Next();
    float pan  = kTidePan * tide_ * tide_pan_.Next();
    float gain = FastExp2(tide_db_ * (1.0f / 6.0206f));
    tide_l_    = gain * (1.0f - fmaxf(pan, 0.0f));
    tide_r_    = gain * (1.0f + fminf(pan, 0.0f));

    out_side_ += fast * (kOutSide[width] - out_side_);
    in_side_ += fast * (kInSide[width] - in_side_);
    sea_.SetInputSide(in_side_);
    // Narrowing drops the side, and the level with it: make it up.
    width_gain_ = 1.0f / sqrtf(0.5f * (1.0f + out_side_ * out_side_));

    // Predelay: a new length fades in from the old.
    int pre = p.predelay < 0 ? 0 : (p.predelay >= kNumPredelays ? kNumPredelays - 1 : p.predelay);
    if(pre != pre_index_)
    {
        float frames = kPredelayMs[pre] * 0.001f * sample_rate_;
        if(pre_index_ >= 0)
        {
            // Mid-fade, it starts again from whichever read was louder.
            if(pre_fade_ >= 0.5f)
                pre_from_ = pre_to_;
            pre_fade_ = 0.0f;
        }
        pre_to_    = frames;
        pre_index_ = pre;
    }

    gate_target_   = p.active && !p.freeze ? 1.0f : 0.0f;
    bypass_target_ = p.active ? 0.0f : 1.0f;
    cut_target_    = cut ? 1.0f : 0.0f;
    kill_target_   = p.kill_dry ? 1.0f : 0.0f;

    // Mix, equal power.
    mix_ += coef_.mix * (Clamp(p.mix, 0.0f, 1.0f) - mix_);
    dry_ = FastSin(0.25f + 0.25f * mix_); // cos(mix * pi / 2)
    wet_ = FastSin(0.25f * mix_) * kWetGain;
}

void StormEngine::Process(float in_l, float in_r, float *out_l, float *out_r)
{
    if(gate_ < gate_target_)
        gate_ = fminf(gate_ + gate_step_, gate_target_);
    else if(gate_ > gate_target_)
        gate_ = fmaxf(gate_ - gate_step_, gate_target_);
    float l = low_cut_[0].Process(in_l * gate_);
    float r = low_cut_[1].Process(in_r * gate_);

    // The predelay, read after the write so 0 ms is no delay.
    constexpr uint32_t kMask = kPredelayFrames - 1;
    pre_[2 * pre_pos_]       = l;
    pre_[2 * pre_pos_ + 1]   = r;
    uint32_t to              = (pre_pos_ - (uint32_t)pre_to_) & kMask;
    l                        = pre_[2 * to];
    r                        = pre_[2 * to + 1];
    if(pre_fade_ < 1.0f)
    {
        uint32_t from = (pre_pos_ - (uint32_t)pre_from_) & kMask;
        l             = pre_[2 * from] + pre_fade_ * (l - pre_[2 * from]);
        r             = pre_[2 * from + 1] + pre_fade_ * (r - pre_[2 * from + 1]);
        pre_fade_ += pre_step_;
    }
    pre_pos_ = (pre_pos_ + 1) & kMask;

    float wet_l, wet_r;
    sea_.Process(0.5f * (l + r), 0.5f * (l - r), &wet_l, &wet_r);

    float mid  = 0.5f * width_gain_ * (wet_l + wet_r);
    float side = 0.5f * width_gain_ * (wet_l - wet_r);
    float bp;
    wet_l = tide_l_ * lp_[0].Process(mid + out_side_ * side, &bp);
    wet_r = tide_r_ * lp_[1].Process(mid - out_side_ * side, &bp);
    wet_peak_ = fmaxf(wet_peak_, fmaxf(fabsf(wet_l), fabsf(wet_r)));

    // Bypassed, the dry goes to unity, and the wet either rings out
    // (trails) or is cut. Kill dry drops the dry whatever else.
    bypass_ = Clamp(bypass_ + (bypass_ < bypass_target_ ? fade_step_ : -fade_step_), 0.0f, 1.0f);
    cut_    = Clamp(cut_ + (cut_ < cut_target_ ? fade_step_ : -fade_step_), 0.0f, 1.0f);
    kill_   = Clamp(kill_ + (kill_ < kill_target_ ? fade_step_ : -fade_step_), 0.0f, 1.0f);

    float dry = (dry_ + bypass_ * (1.0f - dry_)) * (1.0f - kill_);
    float wet = wet_ * (1.0f - cut_);
    *out_l    = in_l * dry + wet_l * wet;
    *out_r    = in_r * dry + wet_r * wet;
}

StormEngine::Probe StormEngine::GetProbe() const
{
    Probe p;
    p.level = sea_.Level();
    p.tide  = tide_db_;
    p.rt60  = expf(log_rt60_);
    p.hold  = hold_;
    return p;
}

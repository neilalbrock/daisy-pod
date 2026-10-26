#include "micro_looper.h"

#include <cmath>

#include "dsp.h"

namespace
{
// MODIFY in Tape mode: the speeds, and where each zone starts in MIDI
// values (from MOOD's MIDI manual), so CC 19 lands on the same speeds.
constexpr float kSpeeds[8]    = {-4.0f, -2.0f, -1.0f, -0.5f, 0.5f, 1.0f, 2.0f, 4.0f};
constexpr int   kZoneStart[7] = {5, 24, 43, 62, 85, 98, 120};

// MODIFY in Stretch mode: how fast the grains' position moves, at points in
// MIDI values. These are the middles of MOOD's zones (none, 1.5x, 2x and 4x
// stretched, either way, and stalled across 61..80), joined up so the knob
// moves smoothly between them.
constexpr int   kRatePoints        = 11;
constexpr float kRateAt[kRatePoints] = {0.0f, 7.5f, 23.0f, 38.5f, 53.5f, 61.0f,
                                        80.0f, 88.5f, 104.0f, 119.0f, 127.0f};
constexpr float kRate[kRatePoints]   = {-1.0f, -1.0f, -0.667f, -0.5f, -0.25f, 0.0f,
                                        0.0f, 0.25f, 0.5f, 0.667f, 1.0f};

// Synced LENGTH in Tape mode: whole fractions of the loop, where each zone
// starts in MIDI values (MOOD's synced LENGTH table).
constexpr float kSyncedFractions[6] = {1.0f / 32, 1.0f / 16, 1.0f / 8, 1.0f / 4, 1.0f / 2, 1.0f};
constexpr int   kSyncedStart[5]     = {13, 37, 62, 85, 111};

constexpr float kFadeSeconds    = 0.005f; // engage, release, overdub punch in/out, mode change
constexpr float kReleaseSeconds = 0.05f;  // Env's detector
constexpr float kChopSeconds    = 0.002f; // CLASSIC's gate in Tape
constexpr float kMinLoop        = 2048.0f; // frames: SYNC's shortest loop

// FADE: what each overdub pass keeps, from this at min to all of it.
constexpr float kMinKeep = 0.3f;

// CLASSIC: each pass darkens the loop a little and rounds off its peaks.
constexpr float kWearSmooth = 0.12f;
constexpr float kWearDrive  = 1.1f;

// SPREAD's pans, in cycles per core tick at 48k.
constexpr float kStretchPanMin = 0.05f / 48000.0f, kStretchPanMax = 0.5f / 48000.0f;

// LENGTH's ranges in core frames (seconds at 48k).
constexpr float kMinSlice = 240.0f, kMaxSlice = 48000.0f; // Env: 5 ms to 1 s
constexpr float kMinGrain = 96.0f, kMaxGrain = 48000.0f;  // Stretch: 2 ms to 1 s
} // namespace

void MicroLooper::Init(float *data, size_t frames)
{
    buffer_.Init(data, frames);
    loop_len_ = (float)(frames - kSeam);
    SetRate(48000.0f);
    SetLength(length_);
    SetModify(modify_);
}

void MicroLooper::SetRate(float core_rate)
{
    // These run every block: skip the maths unless something changed.
    if(core_rate == rate_set_)
        return;
    rate_set_ = core_rate;
    gain_inc_    = 1.0f / (kFadeSeconds * core_rate);
    env_release_ = expf(-1.0f / (kReleaseSeconds * core_rate));
    chop_coef_   = fminf(1.0f, 1.0f / (kChopSeconds * core_rate));
}

void MicroLooper::SetLength(float length)
{
    length_ = dsp::Clamp(length, 0.0f, 1.0f);
    if(length_ == length_set_ && loop_len_ == loop_len_set_ && options_.synced == synced_set_)
        return;
    length_set_   = length_;
    loop_len_set_ = loop_len_;
    synced_set_   = options_.synced;

    // Tape: full at max, down to 1/32 of the loop at min, or in whole
    // fractions while synced.
    if(options_.synced)
    {
        int v    = (int)(length_ * 127.0f + 0.5f);
        int zone = 0;
        while(zone < 5 && v >= kSyncedStart[zone])
            zone++;
        window_ = loop_len_ * kSyncedFractions[zone];
    }
    else
        window_ = loop_len_ * exp2f(5.0f * (length_ - 1.0f));

    slice_len_  = kMinSlice * powf(kMaxSlice / kMinSlice, length_);
    grain_size_ = kMinGrain * powf(kMaxGrain / kMinGrain, length_);
}

void MicroLooper::SetModify(float modify)
{
    modify_ = dsp::Clamp(modify, 0.0f, 1.0f);
    if(modify_ == modify_set_)
        return;
    modify_set_ = modify_;
    float v = modify_ * 127.0f;

    int zone = 0;
    while(zone < 7 && (int)(v + 0.5f) >= kZoneStart[zone])
        zone++;
    speed_ = kSpeeds[zone];

    // Env: the lower the knob, the louder you have to play.
    threshold_ = dsp::DbToGain(-12.0f - 48.0f * modify_);

    int i = 1;
    while(i < kRatePoints - 1 && v > kRateAt[i])
        i++;
    float t = (v - kRateAt[i - 1]) / (kRateAt[i] - kRateAt[i - 1]);
    rate_   = kRate[i - 1] + dsp::Clamp(t, 0.0f, 1.0f) * (kRate[i] - kRate[i - 1]);
    if(rate_ != 0.0f)
        dir_ = rate_ < 0.0f ? -1.0f : 1.0f;
}

void MicroLooper::Engage()
{
    // The ring's newest loop_len_ frames, with the seam just before them:
    // all of the ring, half of it, or what SYNC asks for.
    float ring = (float)(options_.half ? buffer_.Frames() / 2 : buffer_.Frames()) - (float)kSeam;
    loop_len_  = options_.capture > 0 ? dsp::Clamp((float)options_.capture, kMinLoop, ring) : ring;
    SetLength(length_); // the window, slices and grains follow the loop
    start_ = buffer_.WritePos() - (uint32_t)loop_len_;
    mode_  = next_mode_;
    Begin();
    mode_gain_ = 1.0f;
    playing_   = true;
    loop_starts_++;
}

void MicroLooper::Begin()
{
    repeating_ = false;
    switch(mode_)
    {
        case Mode::kTape: pos_ = speed_ < 0.0f ? TapeWindow() - 1.0f : 0.0f; break;
        case Mode::kEnv: pos_ = 0.0f; break;
        case Mode::kStretch:
            pos_ = dir_ < 0.0f ? loop_len_ - 1.0f : 0.0f;
            StartGrains();
            break;
    }
}

void MicroLooper::Release()
{
    playing_    = false;
    dub_target_ = 0.0f;
}

void MicroLooper::SetOverdub(bool on)
{
    dub_target_ = on && playing_ ? 1.0f : 0.0f;
}

float MicroLooper::LoopPos() const
{
    return mode_ == Mode::kTape ? loop_len_ - TapeWindow() + pos_ : pos_;
}

void MicroLooper::SetLoopPos(float pos)
{
    if(mode_ == Mode::kTape)
    {
        // Into the window, which is the end of the loop.
        float window = TapeWindow();
        pos          = fmodf(pos - (loop_len_ - window), window);
        if(pos < 0.0f)
            pos += window;
    }
    pos_ = pos;
}

void MicroLooper::SwitchMode(Mode mode)
{
    // Carry on from the same place in the loop.
    float pos  = LoopPos();
    mode_      = mode;
    repeating_ = false;
    SetLoopPos(pos);
    if(mode_ == Mode::kStretch)
        StartGrains();
}

void MicroLooper::StartGrains()
{
    // The second grain as if it had started half a grain ago, so the two
    // are already in step.
    float half = 0.5f * grain_size_;
    float back = pos_ - rate_ * half;
    if(back < 0.0f)
        back += loop_len_;
    else if(back >= loop_len_)
        back -= loop_len_;
    grains_[0] = {pos_, 0.0f, grain_size_, dir_};
    grains_[1] = {back, half, grain_size_, dir_};
}

uint32_t MicroLooper::ReadSeamless(uint32_t first, float len, float q, float *l, float *r) const
{
    float    whole = floorf(q);
    uint32_t frame = first + (uint32_t)whole;
    buffer_.Read(frame, q - whole, l, r);

    // Near the end, fade into what comes just before the start, so the
    // wrap lands on the same sound.
    float seam = fminf((float)kSeam, len * 0.25f);
    float into = q - (len - seam);
    if(into > 0.0f)
    {
        float before = q - len; // -seam..0
        float bw     = floorf(before);
        float bl, br;
        buffer_.Read(first + (uint32_t)(int32_t)bw, before - bw, &bl, &br);
        float x = into / seam;
        *l += x * (bl - *l);
        *r += x * (br - *r);
    }
    return frame;
}

void MicroLooper::Sense(float l, float r)
{
    float a = fmaxf(fabsf(l), fabsf(r));
    env_    = a > env_ ? a : env_ * env_release_;
}

void MicroLooper::Read(float *l, float *r)
{
    float target = playing_ ? 1.0f : 0.0f;
    if(gain_ < target)
        gain_ = fminf(gain_ + gain_inc_, target);
    else if(gain_ > target)
        gain_ = fmaxf(gain_ - gain_inc_, target);
    if(gain_ <= 0.0f)
    {
        if(next_mode_ != mode_)
            SwitchMode(next_mode_);
        *l = *r = 0.0f;
        return;
    }

    // A mode change fades out, switches, then fades back in.
    if(next_mode_ != mode_)
    {
        mode_gain_ -= gain_inc_;
        if(mode_gain_ <= 0.0f)
        {
            mode_gain_ = 0.0f;
            SwitchMode(next_mode_);
        }
    }
    else if(mode_gain_ < 1.0f)
        mode_gain_ = fminf(mode_gain_ + gain_inc_, 1.0f);

    switch(mode_)
    {
        case Mode::kTape: ReadTape(l, r); break;
        case Mode::kEnv: ReadEnv(l, r); break;
        case Mode::kStretch: ReadStretch(l, r); break;
    }
    float g = gain_ * mode_gain_;
    // CLASSIC with NO DUB: the loop is muted while overdubbing, like MOOD MKI.
    if(options_.classic && options_.no_dub)
        g *= 1.0f - dub_;
    *l *= g;
    *r *= g;
}

void MicroLooper::Pan(float *l, float *r, float amount) const
{
    if(amount <= 0.0f)
        return;
    float m = 0.5f * (*l + *r);
    float p = dsp::FastSin(pan_phase_);
    *l += amount * (m * (1.0f - p) - *l);
    *r += amount * (m * (1.0f + p) - *r);
}

void MicroLooper::ReadTape(float *l, float *r)
{
    // The window can shrink under the play head. It ends where the loop
    // does, so a short one holds the newest audio. CLASSIC plays all of the
    // loop and chops it into pieces of the window's length instead.
    float window = TapeWindow();
    if(pos_ >= window)
        pos_ = fmodf(pos_, window);
    uint32_t first = start_ + (uint32_t)(loop_len_ - window);
    dub_frame_     = ReadSeamless(first, window, pos_, l, r);

    // SPREAD: the left channel plays the same window backwards.
    if(options_.spread)
    {
        float back = window - pos_;
        if(back >= window)
            back -= window;
        float bl, br;
        ReadSeamless(first, window, back, &bl, &br);
        *l = bl;
    }

    if(options_.classic)
    {
        float gate = window_ >= loop_len_ || fmodf(pos_, window_) < 0.5f * window_ ? 1.0f : 0.0f;
        chop_ += chop_coef_ * (gate - chop_);
        *l *= chop_;
        *r *= chop_;
    }

    pos_ += speed_;
    if(pos_ >= window)
    {
        pos_ -= window;
        loop_starts_++;
    }
    else if(pos_ < 0.0f)
    {
        pos_ += window;
        loop_starts_++;
    }
}

void MicroLooper::ReadEnv(float *l, float *r)
{
    // Start or stop repeating only where reading the slice and reading the
    // loop give the same sample: outside the slice's fade and the loop's.
    bool in_loop_seam = pos_ >= loop_len_ - (float)kSeam;
    if(repeating_)
    {
        float seam = fminf((float)kSeam, repeat_len_ * 0.25f);
        if(env_ < threshold_ * 0.5f && pos_ - slice_ < repeat_len_ - seam && !in_loop_seam)
            repeating_ = false;
    }
    else if(env_ > threshold_ && !in_loop_seam)
    {
        // Whole frames, so the slice reads exactly what the loop does.
        float len   = floorf(fminf(slice_len_, loop_len_));
        float slice = floorf(pos_ / len) * len;
        if(slice + len > loop_len_)
            slice = loop_len_ - len;
        float seam = fminf((float)kSeam, len * 0.25f);
        if(pos_ - slice < len - seam)
        {
            repeating_  = true;
            slice_      = slice;
            repeat_len_ = len;
        }
    }

    if(repeating_)
    {
        dub_frame_ = ReadSeamless(start_ + (uint32_t)slice_, repeat_len_, pos_ - slice_, l, r);
        pos_ += 1.0f;
        if(pos_ >= slice_ + repeat_len_)
            pos_ -= repeat_len_;
    }
    else
    {
        dub_frame_ = ReadSeamless(start_, loop_len_, pos_, l, r);
        pos_ += 1.0f;
        if(pos_ >= loop_len_)
        {
            pos_ -= loop_len_;
            loop_starts_++;
        }
    }

    // SPREAD: pans side to side while it repeats, once every two slices.
    if(options_.spread)
    {
        pan_phase_ = dsp::WrapPhase(pan_phase_ + 0.5f / fmaxf(slice_len_, 1.0f));
        float target = repeating_ ? 1.0f : 0.0f;
        pan_amount_ += dsp::Clamp(target - pan_amount_, -gain_inc_, gain_inc_);
        Pan(l, r, pan_amount_);
    }
}

void MicroLooper::ReadStretch(float *l, float *r)
{
    float sum_l = 0.0f, sum_r = 0.0f, weights = 0.0f, loudest = -1.0f;
    for(Grain &g : grains_)
    {
        if(g.age >= g.size)
            g = {pos_, 0.0f, grain_size_, dir_};

        float q = g.start + g.dir * g.age;
        if(q >= loop_len_)
            q -= loop_len_;
        else if(q < 0.0f)
            q += loop_len_;
        float    gl, gr;
        uint32_t frame = ReadSeamless(start_, loop_len_, q, &gl, &gr);

        float s = dsp::FastSin(0.5f * g.age / g.size);
        float w = s * s; // Hann
        sum_l += w * gl;
        sum_r += w * gr;
        weights += w;
        if(w > loudest)
        {
            loudest    = w;
            dub_frame_ = frame;
        }
        g.age += 1.0f;
    }

    // Two grains half a grain apart sum to 1; this keeps the level steady
    // while a LENGTH change has them out of step.
    float norm = 1.0f / fmaxf(weights, 0.5f);
    *l         = sum_l * norm;
    *r         = sum_r * norm;

    // SPREAD: a slow pan, quicker the faster the grains move on.
    if(options_.spread)
    {
        float speed = kStretchPanMin + fabsf(rate_) * (kStretchPanMax - kStretchPanMin);
        pan_phase_  = dsp::WrapPhase(pan_phase_ + speed);
        Pan(l, r, 1.0f);
    }

    pos_ += rate_;
    if(pos_ >= loop_len_)
    {
        pos_ -= loop_len_;
        loop_starts_++;
    }
    else if(pos_ < 0.0f)
    {
        pos_ += loop_len_;
        loop_starts_++;
    }
}

void MicroLooper::Write(float l, float r)
{
    if(!playing_)
    {
        buffer_.Write(l, r);
        return;
    }

    if(dub_ < dub_target_)
        dub_ = fminf(dub_ + gain_inc_, dub_target_);
    else if(dub_ > dub_target_)
        dub_ = fmaxf(dub_ - gain_inc_, dub_target_);

    // Once per frame the play head passes, so slow speeds don't add the
    // same frame twice. Layers pile up, so round them off above full scale
    // (nearly linear below it).
    bool wear = options_.classic;
    if((dub_ <= 0.0f && !wear) || dub_frame_ == last_dubbed_)
        return;
    float  keep = options_.no_dub ? 0.0f : kMinKeep + (1.0f - kMinKeep) * options_.fade;
    keep        = 1.0f - dub_ * (1.0f - keep);
    float  in[2] = {l, r};
    float *f     = buffer_.At(dub_frame_);
    for(int ch = 0; ch < 2; ch++)
    {
        float x = f[ch];
        if(wear)
        {
            // Smoothed against the frame before, and its peaks rounded off.
            x += kWearSmooth * (wear_[ch] - x);
            x         = dsp::SoftClip(kWearDrive * x) / kWearDrive;
            wear_[ch] = x;
        }
        f[ch] = 2.0f * dsp::SoftClip(0.5f * (x * keep + in[ch] * dub_));
    }
    last_dubbed_ = dub_frame_;
}

void MicroLooper::Snapshot(View *v) const
{
    v->playing   = playing_ || gain_ > 0.0f;
    v->mode      = mode_;
    v->write     = buffer_.WritePos();
    v->start     = start_;
    v->loop_len  = loop_len_;
    v->pos       = start_ + (uint32_t)LoopPos();
    v->env       = env_;
    v->threshold = threshold_;
    v->num_heads = 0;
    if(!v->playing)
        return;
    switch(mode_)
    {
        case Mode::kTape:
        {
            float    window = TapeWindow();
            uint32_t first  = start_ + (uint32_t)(loop_len_ - window);
            v->heads[0] = {first, window, first + (uint32_t)pos_, fabsf(speed_), speed_ < 0.0f, 1.0f};
            v->num_heads = 1;
            break;
        }
        case Mode::kEnv:
            if(repeating_)
                v->heads[0] = {start_ + (uint32_t)slice_, repeat_len_, v->pos, 1.0f, false, 1.0f};
            else
                v->heads[0] = {start_, loop_len_, v->pos, 1.0f, false, 1.0f};
            v->num_heads = 1;
            break;
        case Mode::kStretch:
            for(const Grain &g : grains_)
            {
                float q = g.start + g.dir * g.age;
                if(q >= loop_len_)
                    q -= loop_len_;
                else if(q < 0.0f)
                    q += loop_len_;
                float s     = dsp::FastSin(0.5f * g.age / g.size);
                float first = g.dir > 0.0f ? g.start : g.start - g.size;
                if(first < 0.0f)
                    first += loop_len_;
                v->heads[v->num_heads++]
                    = {start_ + (uint32_t)first, g.size, start_ + (uint32_t)q, 1.0f, g.dir < 0.0f, s * s};
            }
            break;
    }
}

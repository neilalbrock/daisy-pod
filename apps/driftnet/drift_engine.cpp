#include "drift_engine.h"

#include <cmath>

namespace
{
constexpr float kFadeSeconds   = 0.005f; // the Wet Channel on and off, and each half of a mode change
constexpr float kSmoothSeconds = 0.05f;  // SMOOTH's glide between clock rates
constexpr float kBlockFrames   = 4.0f;   // SetParams runs once per block of this many samples

// TONE's hi-cut, from this at min to (nearly) open.
constexpr float kToneMinHz = 800.0f, kToneMaxHz = 20000.0f;

// Note values in beats: 1/32, 1/16, 1/8 triplet, 1/8, dotted 1/8, 1/4, 1/2
// and 1. The first six are TIME's synced steps, starting at these MIDI
// values (MOOD's synced TIME table); all eight are the clock divisions.
constexpr float kNoteBeats[8]     = {0.125f, 0.25f, 1.0f / 3.0f, 0.5f, 0.75f, 1.0f, 2.0f, 4.0f};
constexpr int   kDivisionStart[5] = {13, 37, 62, 85, 111};
constexpr int   kQuarter          = 5;

float NoteBeats(int division)
{
    return kNoteBeats[division < 0 ? 0 : (division > 7 ? 7 : division)];
}

// SYNC with the wet following the loop: TIME steps through these fractions
// of the loop, in equal parts of the knob.
constexpr float kLoopFractions[8] = {1.0f / 32, 1.0f / 16, 1.0f / 8, 1.0f / 6,
                                     1.0f / 4,  1.0f / 3,  1.0f / 2, 1.0f};

// CLASSIC: hiss, at 64k, rising as the clock comes down.
constexpr float kNoiseAt64k = 0.0005f;
} // namespace

void DriftEngine::Init(float sample_rate, const Buffers &buffers)
{
    sample_rate_ = sample_rate;
    clock_.Init(sample_rate);
    looper_.Init(buffers.loop, buffers.loop_frames);
    delay_.Init(buffers.delay, buffers.delay_frames);
    slip_.Init(buffers.slip, buffers.slip_frames);
    reverb_.Init(buffers.reverb, buffers.reverb_frames);
    smooth_coef_ = dsp::SmoothCoef(kSmoothSeconds, sample_rate / kBlockFrames);
    dry_inc_     = 1.0f / (kFadeSeconds * sample_rate);
    SetParams(params_);
}

float DriftEngine::SyncedTime(float rate) const
{
    const Params &p = params_;
    if(p.sync == Sync::kWetToLoop)
    {
        int zone = (int)(dsp::Clamp(p.time, 0.0f, 1.0f) * 7.999f);
        return looper_.PlayedLength() * kLoopFractions[zone];
    }
    if(p.samples_per_beat > 0.0f)
    {
        int v    = (int)(dsp::Clamp(p.time, 0.0f, 1.0f) * 127.0f + 0.5f);
        int zone = 0;
        while(zone < 5 && v >= kDivisionStart[zone])
            zone++;
        // TIME's note value, times the clock division (a quarter note leaves
        // it as it is). Halve a time too long for the ring, as MOOD does.
        float beats  = kNoteBeats[zone] * NoteBeats(p.time_division) / kNoteBeats[kQuarter];
        float frames = beats * p.samples_per_beat / sample_rate_ * rate;
        while(frames > delay_.MaxFrames())
            frames *= 0.5f;
        return frames;
    }
    return 0.0f;
}

void DriftEngine::SetParams(const Params &p)
{
    params_ = p;

    // The clock: Synth Mode's rate, SMOOTH's glide, or the knob's step.
    if(p.clock_hz > 0.0f)
    {
        clock_.SetHz(p.clock_hz);
        smooth_hz_ = clock_.Rate();
    }
    else if(p.smooth)
    {
        float target = ClockDomain::kMinRate * powf(ClockDomain::kMaxRate / ClockDomain::kMinRate,
                                                    dsp::Clamp(p.clock, 0.0f, 1.0f));
        smooth_hz_ += smooth_coef_ * (target - smooth_hz_);
        clock_.SetHz(smooth_hz_);
    }
    else
    {
        clock_.SetKnob(p.clock);
        smooth_hz_ = clock_.Rate();
    }
    float rate = clock_.Rate();
    wet_inc_   = 1.0f / (kFadeSeconds * rate);

    float synced = SyncedTime(rate);

    MicroLooper::Options lo;
    lo.fade    = p.fade;
    lo.no_dub  = p.no_dub;
    lo.classic = p.classic;
    lo.half    = p.half_length;
    lo.synced  = p.samples_per_beat > 0.0f
                     ? NoteBeats(p.length_division) * p.samples_per_beat / sample_rate_ * rate
                     : 0.0f;
    lo.spread  = p.spread && p.solo != Solo::kWet;
    if(p.sync == Sync::kLoopToWet)
        lo.capture = (uint32_t)(synced > 0.0f ? synced : delay_.TimeFrames(p.time));
    looper_.SetRate(rate);
    looper_.SetMode(p.loop_mode);
    looper_.SetOptions(lo);
    looper_.SetLength(p.length);
    looper_.SetModify(p.loop_modify);

    float width = p.spread && p.solo != Solo::kLoop ? dsp::Clamp(p.width, 0.0f, 1.0f) : 0.0f;

    delay_.SetRate(rate);
    if(synced > 0.0f)
        delay_.SetTimeFrames(synced);
    else
        delay_.SetTime(p.time);
    delay_.SetFeedback(p.wet_modify);
    delay_.SetFreeze(p.freeze);
    delay_.SetSpread(width);
    delay_.SetClassic(p.classic);

    reverb_.SetRate(rate);
    reverb_.SetTime(p.time);
    reverb_.SetModify(p.wet_modify);
    reverb_.SetFreeze(p.freeze);
    reverb_.SetSpread(width);
    reverb_.SetClassic(p.classic);

    if(synced > 0.0f)
        slip_.SetWindowFrames(synced);
    else
        slip_.SetTime(p.time);
    slip_.SetModify(p.wet_modify);
    slip_.SetFreeze(p.freeze);
    slip_.SetSpread(width);
    slip_.SetClassic(p.classic);

    // TONE, worked out again only when it or the clock moves, so the
    // cutoff stays put in Hz.
    float tone = dsp::Clamp(p.tone, 0.0f, 1.0f);
    tone_on_   = tone < 0.995f;
    if(tone_on_ && (tone != tone_set_ || rate != tone_rate_))
    {
        float hz = kToneMinHz * powf(kToneMaxHz / kToneMinHz, tone);
        for(dsp::Biquad &b : tone_)
            b.Set(dsp::Biquad::Type::kLowPass, hz, 0.707f, 0.0f, rate);
        tone_set_  = tone;
        tone_rate_ = rate;
    }

    float balance = dsp::Clamp(p.balance, 0.0f, 1.0f);
    wet_bal_      = fminf(1.0f, 2.0f * (1.0f - balance));
    loop_bal_     = fminf(1.0f, 2.0f * balance);

    // Both at full at noon: Mix turns one down, never both up.
    float mix = dsp::Clamp(p.mix, 0.0f, 1.0f);
    dry_      = p.dry_kill ? 0.0f : fminf(1.0f, 2.0f * (1.0f - mix));
    wet_      = fminf(1.0f, 2.0f * mix);

    noise_ = p.classic ? kNoiseAt64k * ClockDomain::kMaxRate / rate : 0.0f;
}

void DriftEngine::TakePeaks(float *wet, float *loop)
{
    *wet       = peak_wet_;
    *loop      = peak_loop_;
    peak_wet_  = 0.0f;
    peak_loop_ = 0.0f;
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

    // What the Wet Channel hears follows its switch; what's heard of it
    // stays up after it's off with TRAILS, so it rings out.
    float send = params_.wet_on ? 1.0f : 0.0f;
    float out  = params_.wet_on || params_.trails ? 1.0f : 0.0f;
    send_gain_ += dsp::Clamp(send - send_gain_, -wet_inc_, wet_inc_);
    out_gain_ += dsp::Clamp(out - out_gain_, -wet_inc_, wet_inc_);

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
    ProcessWet(send_l * send_gain_, send_r * send_gain_, &wet_l, &wet_r);
    if(tone_on_)
    {
        wet_l = tone_[0].Process(wet_l);
        wet_r = tone_[1].Process(wet_r);
    }
    float wet_g = out_gain_ * wet_bal_;
    wet_l *= wet_g;
    wet_r *= wet_g;

    // Listening, the looper catches the Wet Channel too; overdubs take only
    // the clean input, or the loop would feed itself through the effect.
    if(looper_.Playing())
        looper_.Write(in_l, in_r);
    else if(params_.listen)
        looper_.Write(in_l + wet_l, in_r + wet_r);

    // A loop sent through the Wet Channel is heard only through it, unless
    // DIRECT MICRO-LOOP blends some of it back.
    float direct = params_.routing == Routing::kIn
                       ? 1.0f
                       : 1.0f - send_gain_ * (1.0f - params_.direct);
    float loop_g = direct * loop_bal_;
    loop_l *= loop_g;
    loop_r *= loop_g;

    peak_wet_  = fmaxf(peak_wet_, fmaxf(fabsf(wet_l), fabsf(wet_r)));
    peak_loop_ = fmaxf(peak_loop_, fmaxf(fabsf(loop_l), fabsf(loop_r)));

    *out_l = wet_l + loop_l;
    *out_r = wet_r + loop_r;
    if(noise_ > 0.0f)
    {
        *out_l += noise_ * rng_.Bi();
        *out_r += noise_ * rng_.Bi();
    }
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

    // With both channels off the dry is at unity, as in bypass (TRAILS can
    // keep the wet going over it). Synth Mode's gate shapes the wet and loop.
    float dry = params_.dry_kill ? 0.0f : (Idle() ? 1.0f : dry_);
    dry_now_ += dsp::Clamp(dry - dry_now_, -dry_inc_, dry_inc_);
    gate_now_ += 0.05f * (params_.gate - gate_now_);
    float wet = wet_ * gate_now_;
    *out_l    = in_l * dry_now_ + l * wet;
    *out_r    = in_r * dry_now_ + r * wet;
}

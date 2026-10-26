#include "synth.h"

#include <cmath>

#include "dsp.h"

namespace
{
constexpr float kUnityHz     = 48000.0f; // the rate middle C plays at, at octave 3
constexpr float kBendRange   = 4.0f;     // semitones either way
constexpr float kVibratoHz   = 5.0f;
constexpr float kVibratoMax  = 0.5f; // semitones, at full mod wheel
constexpr float kMaxGlideS   = 2.0f; // PORTAMENTO at max
constexpr float kMinStageS   = 0.001f;
constexpr float kMaxAttackS  = 4.0f;
constexpr float kMaxDecayS   = 4.0f;
constexpr float kMaxReleaseS = 8.0f;

// 0..1 to seconds, exponential from kMinStageS.
float StageSeconds(float x, float max_s)
{
    return kMinStageS * powf(max_s / kMinStageS, dsp::Clamp(x, 0.0f, 1.0f));
}
} // namespace

void Synth::Init(float block_rate)
{
    block_rate_ = block_rate;
}

void Synth::NoteOn(int note, int velocity)
{
    if(velocity == 0)
    {
        NoteOff(note);
        return;
    }
    // Last note first: a key already held moves to the top.
    NoteOff(note);
    if(held_ == kMaxHeld)
    {
        for(int i = 1; i < kMaxHeld; i++)
            notes_[i - 1] = notes_[i];
        held_--;
    }
    notes_[held_++] = note;
    last_note_      = note;
    velocity_       = (float)velocity / 127.0f;
    if(!active_)
        pitch_ = (float)note; // the first note doesn't glide in
    active_ = true;
    stage_  = Stage::kAttack;
}

void Synth::NoteOff(int note)
{
    for(int i = 0; i < held_; i++)
        if(notes_[i] == note)
        {
            for(int k = i + 1; k < held_; k++)
                notes_[k - 1] = notes_[k];
            held_--;
            break;
        }
    if(held_ == 0 && stage_ != Stage::kIdle)
        stage_ = Stage::kRelease;
}

void Synth::PitchBend(int value)
{
    bend_ = (float)(value - 8192) / 8192.0f * kBendRange;
}

void Synth::Exit()
{
    active_ = false;
    held_   = 0;
    stage_  = Stage::kIdle;
    env_    = 0.0f;
}

void Synth::Update(const Settings &s)
{
    if(!active_)
    {
        gain_ = 1.0f;
        return;
    }

    // Glide towards the note: PORTAMENTO is how long it takes to get most
    // of the way.
    float target = (float)Note();
    if(s.portamento <= 0.001f)
        pitch_ = target;
    else
        pitch_ += dsp::SmoothCoef(s.portamento * kMaxGlideS, block_rate_) * (target - pitch_);

    vibrato_phase_ = dsp::WrapPhase(vibrato_phase_ + kVibratoHz / block_rate_);
    float semis    = pitch_ - 60.0f + 12.0f * (float)(s.octave - 3) + bend_
                  + mod_ * kVibratoMax * dsp::FastSin(vibrato_phase_);
    hz_ = kUnityHz * exp2f(semis / 12.0f);

    // The envelope, used by ADSR (ON/OFF is the same with instant stages).
    float attack  = 1.0f / (StageSeconds(s.attack, kMaxAttackS) * block_rate_);
    float decay   = 1.0f / (StageSeconds(s.decay, kMaxDecayS) * block_rate_);
    float release = 1.0f / (StageSeconds(s.release, kMaxReleaseS) * block_rate_);
    float sustain = dsp::Clamp(s.sustain, 0.0f, 1.0f);
    switch(stage_)
    {
        case Stage::kAttack:
            env_ += attack;
            if(env_ >= 1.0f)
            {
                env_   = 1.0f;
                stage_ = Stage::kDecay;
            }
            break;
        case Stage::kDecay:
            env_ -= decay * (1.0f - sustain);
            if(env_ <= sustain)
            {
                env_   = sustain;
                stage_ = Stage::kSustain;
            }
            break;
        case Stage::kSustain: env_ = sustain; break;
        case Stage::kRelease:
            env_ -= release;
            if(env_ <= 0.0f)
            {
                env_   = 0.0f;
                stage_ = Stage::kIdle;
            }
            break;
        case Stage::kIdle: env_ = 0.0f; break;
    }

    switch(s.output)
    {
        case Output::kOpen: gain_ = 1.0f; break;
        case Output::kOnOff: gain_ = held_ > 0 ? velocity_ : 0.0f; break;
        case Output::kAdsr: gain_ = env_ * velocity_; break;
    }
}

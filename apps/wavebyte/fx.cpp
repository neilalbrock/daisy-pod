#include "fx.h"

void FxChain::Init(float *fast, float *slow, float sample_rate, size_t block_size)
{
    context_.sample_rate = sample_rate;
    context_.block_rate  = sample_rate / (float)block_size;
    fade_step_           = 1.0f / (0.01f * sample_rate);

    reverb_.Init(fast); // clears its tank
    fast += Plate::kFloats;
    chorus_.Init(fast);
    fast += 2 * fx::Chorus::kLine;
    flanger_.Init(fast);
    fast += 2 * fx::Flanger::kLine;
    compressor_.Init(fast);
    delay_.Init(slow);
}

void FxChain::Start(FxType type)
{
    switch(type)
    {
        case kFxChorus: chorus_.Reset(); break;
        case kFxFlanger: flanger_.Reset(); break;
        case kFxPhaser: phaser_.Reset(); break;
        case kFxTremolo: tremolo_.Reset(); break;
        case kFxDrive: drive_.Reset(); break;
        case kFxCompressor: compressor_.Reset(); break;
        case kFxEq: eq_.Reset(); break;
        case kFxDelay: delay_.Reset(); break;
        default: break; // the reverb clears over several blocks: see Set
    }
}

void FxChain::SetEffect(FxType type, const float *values, size_t size)
{
    const float *v = values + kFxFirst[type];
    switch(type)
    {
        case kFxChorus: chorus_.Set(v, context_, size); break;
        case kFxFlanger: flanger_.Set(v, context_, size); break;
        case kFxPhaser: phaser_.Set(v, context_, size); break;
        case kFxTremolo: tremolo_.Set(v, context_, size); break;
        case kFxDrive: drive_.Set(v, context_, size); break;
        case kFxCompressor: compressor_.Set(v, context_, size); break;
        case kFxEq: eq_.Set(v, context_, size); break;
        case kFxDelay: delay_.Set(v, context_, size); break;
        case kFxReverb: reverb_.Set(v, context_, size); break;
        default: break;
    }
}

void FxChain::Run(FxType type, float *l, float *r, size_t size)
{
    switch(type)
    {
        case kFxChorus: chorus_.Process(l, r, size); break;
        case kFxFlanger: flanger_.Process(l, r, size); break;
        case kFxPhaser: phaser_.Process(l, r, size); break;
        case kFxTremolo: tremolo_.Process(l, r, size); break;
        case kFxDrive: drive_.Process(l, r, size); break;
        case kFxCompressor: compressor_.Process(l, r, size); break;
        case kFxEq: eq_.Process(l, r, size); break;
        case kFxDelay: delay_.Process(l, r, size); break;
        case kFxReverb: reverb_.Process(l, r, size); break;
        default: break;
    }
}

void FxChain::Set(const Settings &s, size_t size)
{
    context_.samples_per_beat = s.samples_per_beat;
    for(int i = 0; i < 2; i++)
    {
        Slot  &slot = slots_[i];
        bool   used = (s.routes & (1 << kFxSerial)) || (i == 1 && (s.routes & (1 << kFxOnlyTwo)));
        FxType want = used ? s.type[i] : kFxOff;
        if(i == 1 && s.type[1] == s.type[0])
            want = kFxOff; // a type in one slot at a time

        if(slot.running != want)
        {
            // Out first; once silent, the new one starts.
            slot.target = 0.0f;
            if(slot.fade <= 0.0f)
            {
                slot.running  = want;
                slot.clearing = want == kFxReverb;
                Start(want);
            }
        }
        else if(slot.clearing)
            slot.clearing = !reverb_.Clear();
        else
            slot.target = want == kFxOff ? 0.0f : 1.0f;

        if(slot.running != kFxOff && !slot.clearing)
            SetEffect(slot.running, s.values, size);
    }
}

void FxChain::RunSlot(Slot &slot, float *l, float *r, size_t size)
{
    if(slot.running == kFxOff || slot.clearing || (slot.fade <= 0.0f && slot.target <= 0.0f))
        return;
    float dry_l[16], dry_r[16];
    for(size_t n = 0; n < size; n++)
    {
        dry_l[n] = l[n];
        dry_r[n] = r[n];
    }
    Run(slot.running, l, r, size);
    float fade = slot.fade, step = slot.target > fade ? fade_step_ : -fade_step_;
    if(fade == slot.target)
        step = 0.0f;
    for(size_t n = 0; n < size; n++)
    {
        fade = dsp::Clamp(fade + step, 0.0f, 1.0f);
        l[n] = dsp::Lerp(dry_l[n], l[n], fade);
        r[n] = dsp::Lerp(dry_r[n], r[n], fade);
    }
    slot.fade = fade;
}

void FxChain::Process(float *const l[kNumFxRoutings], float *const r[kNumFxRoutings], size_t size)
{
    float *l1 = l[kFxSerial], *r1 = r[kFxSerial];
    float *l2 = l[kFxOnlyTwo], *r2 = r[kFxOnlyTwo];
    float *out_l = l[kFxDry], *out_r = r[kFxDry];
    RunSlot(slots_[0], l1, r1, size);
    for(size_t n = 0; n < size; n++)
    {
        l2[n] += l1[n];
        r2[n] += r1[n];
    }
    RunSlot(slots_[1], l2, r2, size);
    for(size_t n = 0; n < size; n++)
    {
        out_l[n] += l2[n];
        out_r[n] += r2[n];
    }
}

#include "synth_engine.h"

#include <cmath>

#include "dsp.h"

namespace
{
constexpr float kVoiceLevel = 0.35f; // eight voices summed can still pass full scale: the limiter catches it
} // namespace

float SynthEngine::GlideSeconds(float x)
{
    return x <= 0.0f ? 0.0f : 0.002f * powf(2500.0f, x); // 2 ms to 5 s
}

void SynthEngine::Init(const Wavetables *tables, float sample_rate, size_t block_size)
{
    tables_      = tables;
    sample_rate_ = sample_rate;
    block_rate_  = sample_rate / (float)block_size;
    VoiceFilter::Init(sample_rate);
    Voice::SetFullScale(kVoiceLevel);
    for(int i = 0; i < kVoices; i++)
        voices_[i].Init(tables, sample_rate, block_size, 0x1234567u + 7919u * (uint32_t)i);
    global_lfo_[0].Seed(0x51ed270bu);
    global_lfo_[1].Seed(0x2545f491u);
    frame_.controllers = &controllers_;
    SetPatch(patch_);
}

void SynthEngine::SetPatch(const Patch &p)
{
    // A change of voice mode starts afresh, as does MPE on or off.
    if(p.voice_mode != patch_.voice_mode || p.mpe != controllers_.mpe)
        AllNotesOff();
    if(p.mpe != controllers_.mpe)
    {
        controllers_.mpe = p.mpe;
        for(int c = 0; c < 16; c++)
            controllers_.channel_bend[c] = controllers_.channel_y[c] = controllers_.channel_pressure[c] = 0.0f;
    }
    patch_ = p;

    Voice::Frame &f = frame_;
    for(int o = 0; o < 2; o++)
    {
        const Patch::Osc &po = p.osc[o];
        Voice::Frame::Osc &fo = f.osc[o];
        fo.table    = tables_->Get(po.table, 0);
        fo.wave     = po.wave * (Wavetables::kWaves - 1);
        fo.env      = Bipolar(po.env) * (Wavetables::kWaves - 1);
        fo.velocity = Bipolar(po.velocity) * (Wavetables::kWaves - 1);
        fo.top      = po.limit ? Wavetables::kTableWaves - 1 : Wavetables::kWaves - 1;
        fo.level    = po.level;
        fo.offset   = (float)Semitones(po.semitone) + Detune(po.detune);
        fo.fixed    = po.fixed;
    }

    f.bend            = controllers_.bend * (float)BendRange(p.bend_range);
    f.note_bend_range = BendRange(p.bend_range) > 0 ? 48.0f : 0.0f;
    // The glide and envelope coefficients cost a few powf and expf each:
    // only when their settings move.
    if(p.glide != glide_set_)
    {
        glide_set_    = p.glide;
        float glide_s = GlideSeconds(p.glide);
        f.glide       = glide_s > 0.0f ? 1.0f - expf(-3.0f / (glide_s * block_rate_)) : 1.0f;
    }

    f.filter     = (VoiceFilter::Type)p.filter;
    f.cutoff     = p.cutoff * VoiceFilter::kOctaves;
    f.resonance  = p.resonance;
    f.filter_env = Bipolar(p.filter_env) * 8.0f;
    f.filter_vel = Bipolar(p.filter_vel) * 8.0f;
    f.track      = Bipolar(p.track) * 2.0f;
    f.dirt       = p.dirt * p.dirt;
    f.dirt_knob  = p.dirt;
    f.dirt_type  = (Dirt::Type)p.dirt_type;

    // Flavour's pitch drift on osc 1 is a share of Bend Range, as on the
    // Protein: none at a range of 0.
    f.flavour       = p.flavour;
    f.flavour_pitch = 0.08f * (float)BendRange(p.bend_range);
    f.clean         = p.osc_mode == kOscClean;
    f.dac           = p.dac;
    if(p.dac != dac_set_)
    {
        dac_set_   = p.dac;
        f.dac_step = p.dac > 0.0f ? exp2f(1.0f - DacBits(p.dac)) : 0.0f;
    }

    for(int e = 0; e < Voice::kNumEnvs; e++)
    {
        const float *s = p.env[e];
        float       *was = env_set_[e];
        if(s[0] != was[0] || s[1] != was[1] || s[2] != was[2] || s[3] != was[3])
        {
            float *t = f.env_times[e];
            t[0]     = Adsr::Coefs::AttackSeconds(s[0]);
            t[1]     = Adsr::Coefs::FallSeconds(s[1]);
            t[2]     = s[2];
            t[3]     = Adsr::Coefs::FallSeconds(s[3]);
            f.env[e].SetTimes(t[0], t[1], t[2], t[3], block_rate_);
            for(int i = 0; i < 4; i++)
                was[i] = s[i];
        }
    }

    for(int i = 0; i < 2; i++)
    {
        const Patch::LfoSettings &l = p.lfo[i];
        f.lfo_shape[i] = (Lfo::Shape)Pick(l.shape, Lfo::kNumShapes);
        f.lfo_mode[i]  = (Lfo::Mode)Pick(l.mode, Lfo::kNumModes);
        f.lfo_rate[i]  = 0.02f * dsp::FastExp2(LfoSpeedOctaves() * l.speed) / block_rate_;
        lfo_beats_[i]  = Lfo::kRatioBeats[Pick(l.ratio, Lfo::kNumRatios)];
    }

    // The slots in use, in units.
    f.num_slots = 0;
    for(int e = 0; e < Voice::kNumEnvs; e++)
        f.env_modulated[e] = false;
    for(int i = 0; i < mod::kSlots; i++)
    {
        const Patch::ModSlot &m = p.mod[i];
        auto  source = (mod::Source)Pick(m.source, mod::kNumSources);
        auto  target = (mod::Target)Pick(m.target, mod::kNumTargets);
        float amount = ModAmount(m.amount);
        if(source == mod::kOff || target == mod::kNone || amount == 0.0f)
            continue;
        f.slots[f.num_slots++] = {source, target, amount * mod::kRange[target]};
        if(target >= mod::kEnvStages)
            f.env_modulated[(target - mod::kEnvStages) / 4] = true;
    }

    f.volume       = p.volume * p.volume * kVoiceLevel;
    f.amp_velocity = p.amp_velocity;
    f.pan          = Bipolar(p.pan);
    f.pan_spread   = (float)p.pan_mode / (kNumPanModes - 1);
}

bool SynthEngine::Held(int note) const
{
    for(int i = 0; i < num_held_; i++)
        if(held_[i] == note)
            return true;
    return false;
}

int SynthEngine::Allocate()
{
    // A free voice, else the oldest released, else the oldest.
    int best = -1;
    for(int pass = 0; pass < 3 && best < 0; pass++)
        for(int i = 0; i < kVoices; i++)
        {
            const Voice &v = voices_[i];
            bool fits = pass == 0 ? !v.Active() : (pass == 1 ? v.Released() : true);
            if(fits && (best < 0 || v.Age() < voices_[best].Age()))
                best = i;
        }
    return best;
}

void SynthEngine::Play(Voice &v, int note, float velocity, bool retrigger, bool held_before, int channel)
{
    bool  glide = frame_.glide < 1.0f && (patch_.glide_mode == kGlideOnset || held_before);
    float from  = glide ? last_note_ : (float)note;
    v.Start(note, velocity, from, retrigger, channel);
    v.SetAge(++started_);
    last_voice_ = (int)(&v - voices_);
    last_note_  = (float)note;
}

void SynthEngine::MonoTo(int note, float velocity, bool retrigger, bool held_before, int channel)
{
    Voice &v = voices_[0];
    if(!v.Active())
        retrigger = true;
    // Glide on from wherever the pitch has got to.
    if(v.Active())
        last_note_ = v.Pitch();
    Play(v, note, velocity, retrigger, held_before, channel);
}

void SynthEngine::NoteOn(int note, int velocity, int channel)
{
    if(velocity == 0)
    {
        NoteOff(note, 64, channel);
        return;
    }
    float vel         = (float)velocity / 127.0f;
    bool  held_before = num_held_ > 0;
    velocity_         = vel;
    channel_          = channel;
    sustained_[note & 127] = false;

    // Onto the stack of keys down (a repeat moves to the top).
    for(int i = 0; i < num_held_; i++)
        if(held_[i] == note)
        {
            for(int k = i + 1; k < num_held_; k++)
                held_[k - 1] = held_[k];
            num_held_--;
            break;
        }
    if(num_held_ == kMaxHeld)
    {
        for(int k = 1; k < kMaxHeld; k++)
            held_[k - 1] = held_[k];
        num_held_--;
    }
    held_[num_held_++] = note;

    if(patch_.voice_mode == kPoly)
    {
        // The same note again takes its own voice back (with MPE, only
        // on the same channel).
        int index = -1;
        for(int i = 0; i < kVoices; i++)
            if(voices_[i].Active() && voices_[i].Note() == note
               && (!controllers_.mpe || voices_[i].Channel() == channel))
                index = i;
        if(index < 0)
            index = Allocate();
        Play(voices_[index], note, vel, true, held_before, channel);
    }
    else
        MonoTo(note, vel, patch_.voice_mode == kMono || !held_before, held_before, channel);
}

void SynthEngine::NoteOff(int note, int velocity, int channel)
{
    float release_vel = (float)velocity / 127.0f;
    for(int i = 0; i < num_held_; i++)
        if(held_[i] == note)
        {
            for(int k = i + 1; k < num_held_; k++)
                held_[k - 1] = held_[k];
            num_held_--;
            break;
        }

    if(patch_.voice_mode == kPoly)
    {
        if(sustain_)
        {
            sustained_[note & 127] = true;
            return;
        }
        for(Voice &v : voices_)
            if(v.Note() == note && !v.Released() && (!controllers_.mpe || v.Channel() == channel))
                v.Release(release_vel);
        return;
    }

    // Only letting go of the note sounding changes anything.
    if(voices_[0].Note() != note || voices_[0].Released())
        return;
    if(num_held_ > 0)
    {
        // The highest key still down takes over.
        int highest = held_[0];
        for(int i = 1; i < num_held_; i++)
            if(held_[i] > highest)
                highest = held_[i];
        MonoTo(highest, velocity_, patch_.voice_mode == kMono, true, channel_);
    }
    else if(sustain_)
        sustained_[note & 127] = true;
    else
        voices_[0].Release(release_vel);
}

void SynthEngine::PitchBend(int channel, float amount)
{
    if(controllers_.mpe && channel != 0)
        controllers_.channel_bend[channel & 15] = amount;
    else
        controllers_.bend = amount;
}

void SynthEngine::Pressure(int channel, float amount)
{
    if(controllers_.mpe && channel != 0)
        controllers_.channel_pressure[channel & 15] = amount;
    else
        controllers_.pressure = amount;
}

void SynthEngine::PolyPressure(int note, float amount)
{
    controllers_.poly_pressure[note & 127] = amount;
}

void SynthEngine::Control(int channel, int cc, float value)
{
    mod::Controllers &c = controllers_;
    switch(cc)
    {
        case 1: c.wheel = value; break;
        case 2: c.breath = value; break;
        case 11: c.expression = value; break;
        case 64:
            c.pedal = value;
            Sustain(value >= 0.5f);
            break;
        case 74:
            if(c.mpe && channel != 0)
                c.channel_y[channel & 15] = value;
            break;
        default:
            if(cc >= 22 && cc <= 31)
                c.cc[cc - 22] = value;
            break;
    }
}

void SynthEngine::Clock(float beats, bool restart)
{
    clock_beats_ = beats;
    if(restart)
        clock_restart_ = true;
}

void SynthEngine::Sustain(bool down)
{
    sustain_ = down;
    if(down)
        return;
    for(int n = 0; n < 128; n++)
    {
        if(!sustained_[n])
            continue;
        sustained_[n] = false;
        if(Held(n))
            continue;
        for(Voice &v : voices_)
            if(v.Note() == n && !v.Released())
                v.Release();
    }
}

void SynthEngine::AllNotesOff()
{
    num_held_ = 0;
    for(bool &s : sustained_)
        s = false;
    for(Voice &v : voices_)
        v.Release();
}

void SynthEngine::Process(float *l, float *r, size_t size)
{
    // The global LFOs: Free Global's speed follows the matrix of the voice
    // last played; Clocked follows the tempo.
    for(int i = 0; i < 2; i++)
    {
        Lfo::Mode mode = frame_.lfo_mode[i];
        if(mode == Lfo::kFreeVoice)
            continue;
        if(mode == Lfo::kClocked && clock_restart_)
            global_lfo_[i].Restart();
        float rate = mode == Lfo::kClocked
                         ? clock_beats_ / lfo_beats_[i]
                         : frame_.lfo_rate[i]
                               * dsp::FastExp2(voices_[last_voice_].Mod((mod::Target)(mod::kLfo1Speed + i)));
        controllers_.lfo[i] = global_lfo_[i].Advance(rate, frame_.lfo_shape[i]);
    }
    clock_restart_ = false;

    for(size_t i = 0; i < size; i++)
        l[i] = r[i] = 0.0f;
    for(Voice &v : voices_)
        if(v.Active())
            v.Render(frame_, l, r, size);
}

SynthEngine::Probe SynthEngine::GetProbe() const
{
    const Voice &v = voices_[last_voice_];
    Probe        p;
    p.wave[0] = v.WaveAt(0);
    p.wave[1] = v.WaveAt(1);
    p.cutoff  = v.CutoffHz();
    p.amp     = v.AmpEnv();
    p.note    = v.Note();
    p.drift   = v.DriftCents();
    p.lfo[0]  = v.LfoValue(0);
    p.lfo[1]  = v.LfoValue(1);
    p.voices  = 0;
    for(const Voice &u : voices_)
        if(u.Active())
            p.voices++;
    return p;
}

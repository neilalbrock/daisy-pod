#include "synth_engine.h"

#include <cmath>

#include "dsp.h"

namespace
{
constexpr float kVoiceLevel = 0.35f; // eight voices summed can still pass full scale: the limiter catches it

template <typename L>
bool Held(const L &layer, int note)
{
    for(int i = 0; i < layer.num_held; i++)
        if(layer.held[i] == note)
            return true;
    return false;
}

// Off the stack of keys down, if it's there.
template <typename L>
void Unhold(L &layer, int note)
{
    for(int i = 0; i < layer.num_held; i++)
        if(layer.held[i] == note)
        {
            for(int k = i + 1; k < layer.num_held; k++)
                layer.held[k - 1] = layer.held[k];
            layer.num_held--;
            return;
        }
}
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
    rng_.Seed(0x3c6ef372u);
    SetGlobal(global_);
    for(int l = 0; l < kLayers; l++)
    {
        Layer &ly = layers_[l];
        ly.lfo[0].Seed(0x51ed270bu + 0x9e3779b9u * (uint32_t)l);
        ly.lfo[1].Seed(0x2545f491u + 0x9e3779b9u * (uint32_t)l);
        ly.frame.controllers = &controllers_;
        SetPatch(l, ly.patch);
    }
}

void SynthEngine::SetGlobal(const Global &g)
{
    // A change of layer mode starts afresh, as does MPE on or off; a layer
    // switched off in a Multi mode lets go of its notes.
    bool mpe = g.mpe && g.layer_mode != kMidiSplit;
    if(g.layer_mode != global_.layer_mode || mpe != controllers_.mpe)
        AllNotesOff();
    else if(g.layer_mode != kSingle)
        for(int l = 0; l < kLayers; l++)
            if(global_.active[l] && !g.active[l])
                LayerNotesOff(l);
    if(mpe != controllers_.mpe)
    {
        controllers_.mpe = mpe;
        for(int c = 0; c < 16; c++)
            controllers_.channel_bend[c] = controllers_.channel_y[c] = controllers_.channel_pressure[c] = 0.0f;
    }
    global_ = g;
    if(g.dac != dac_set_)
    {
        dac_set_  = g.dac;
        dac_step_ = g.dac > 0.0f ? exp2f(1.0f - DacBits(g.dac)) : 0.0f;
    }
}

void SynthEngine::SetPatch(int layer, const Patch &p)
{
    Layer &ly = layers_[layer];
    // A change of voice mode starts the layer afresh.
    if(p.voice_mode != ly.patch.voice_mode)
    {
        LayerNotesOff(layer);
        ly.mono_voice = -1;
    }
    ly.patch = p;

    Voice::Frame &f = ly.frame;
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
    if(p.glide != ly.glide_set)
    {
        ly.glide_set  = p.glide;
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

    // Flavour and the DAC are global. Flavour's pitch drift on osc 1 is a
    // share of the layer's Bend Range, as on the Protein: none at a range
    // of 0.
    f.flavour       = global_.flavour;
    f.flavour_pitch = 0.08f * (float)BendRange(p.bend_range);
    f.clean         = p.osc_mode == kOscClean;
    f.dac           = global_.dac;
    f.dac_step      = dac_step_;

    for(int e = 0; e < Voice::kNumEnvs; e++)
    {
        const float *s = p.env[e];
        float       *was = ly.env_set[e];
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
        f.lfo_shape[i]  = (Lfo::Shape)Pick(l.shape, Lfo::kNumShapes);
        f.lfo_mode[i]   = (Lfo::Mode)Pick(l.mode, Lfo::kNumModes);
        f.lfo_rate[i]   = 0.02f * dsp::FastExp2(LfoSpeedOctaves() * l.speed) / block_rate_;
        ly.lfo_beats[i] = Lfo::kRatioBeats[Pick(l.ratio, Lfo::kNumRatios)];
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

bool SynthEngine::Plays(int layer) const
{
    return global_.layer_mode == kSingle ? layer == global_.selected : global_.active[layer];
}

int SynthEngine::SplitLayers(int channel) const
{
    int l    = (channel - global_.split_channel) & 15;
    int mask = l < kLayers && global_.active[l] ? 1 << l : 0;
    if(global_.all_channels && global_.active[0])
        mask |= 1;
    return mask;
}

int SynthEngine::Route(int channel)
{
    int active = 0;
    for(int l = 0; l < kLayers; l++)
        if(global_.active[l])
            active |= 1 << l;
    switch(global_.layer_mode)
    {
        case kLayered: return active;
        case kRoundRobin:
            // The next active layer up from the last one played.
            for(int i = 1; i <= kLayers; i++)
            {
                int l = (robin_ + i) % kLayers;
                if(active & (1 << l))
                {
                    robin_ = l;
                    return 1 << l;
                }
            }
            return 0;
        case kRandomRobin:
        {
            int count = 0;
            for(int l = 0; l < kLayers; l++)
                count += (active >> l) & 1;
            if(count == 0)
                return 0;
            int pick = (int)(rng_.Next() % (uint32_t)count);
            for(int l = 0; l < kLayers; l++)
                if((active & (1 << l)) && pick-- == 0)
                    return 1 << l;
            return 0;
        }
        case kMidiSplit: return SplitLayers(channel);
        default: return 1 << global_.selected;
    }
}

int SynthEngine::Allocate()
{
    // A free voice, else the oldest released, else the oldest, whichever
    // layer has it.
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

void SynthEngine::Play(int layer, int voice, int note, float velocity, bool retrigger, bool held_before,
                       int channel)
{
    Layer &ly = layers_[layer];
    Voice &v  = voices_[voice];
    // Taken from another layer's mono voice: that layer starts afresh.
    for(int l = 0; l < kLayers; l++)
        if(l != layer && layers_[l].mono_voice == voice)
            layers_[l].mono_voice = -1;
    bool  glide = ly.frame.glide < 1.0f && (ly.patch.glide_mode == kGlideOnset || held_before);
    float from  = glide ? ly.last_note : (float)note;
    v.SetLayer(layer);
    v.Start(note, velocity, from, retrigger, channel);
    v.SetAge(++started_);
    ly.last_voice = voice;
    ly.last_note  = (float)note;
}

void SynthEngine::MonoTo(int layer, int note, float velocity, bool retrigger, bool held_before, int channel)
{
    Layer &ly = layers_[layer];
    int    i  = ly.mono_voice;
    if(i < 0 || voices_[i].Layer() != layer)
    {
        i             = Allocate();
        ly.mono_voice = i;
        retrigger     = true;
    }
    Voice &v = voices_[i];
    if(!v.Active())
        retrigger = true;
    // Glide on from wherever the pitch has got to.
    if(v.Active())
        ly.last_note = v.Pitch();
    Play(layer, i, note, velocity, retrigger, held_before, channel);
}

void SynthEngine::NoteOn(int note, int velocity, int channel)
{
    if(velocity == 0)
    {
        NoteOff(note, 64, channel);
        return;
    }
    note &= 127;
    int layers = Route(channel);
    note_layers_[note] |= (uint8_t)layers;
    float vel = (float)velocity / 127.0f;
    for(int l = 0; l < kLayers; l++)
        if(layers & (1 << l))
            LayerNoteOn(l, note, vel, channel);
}

void SynthEngine::NoteOff(int note, int velocity, int channel)
{
    note &= 127;
    // In MIDI Split the channel says which layer; otherwise the layers the
    // note went to.
    int layers = note_layers_[note];
    if(global_.layer_mode == kMidiSplit)
        layers &= SplitLayers(channel);
    note_layers_[note] &= (uint8_t)~layers;
    float release_vel = (float)velocity / 127.0f;
    for(int l = 0; l < kLayers; l++)
        if(layers & (1 << l))
            LayerNoteOff(l, note, release_vel, channel);
}

void SynthEngine::LayerNoteOn(int layer, int note, float vel, int channel)
{
    Layer &ly          = layers_[layer];
    bool   held_before = ly.num_held > 0;
    ly.velocity        = vel;
    ly.channel         = channel;
    sustained_[note] &= (uint8_t)~(1 << layer);

    // Onto the stack of keys down (a repeat moves to the top).
    Unhold(ly, note);
    if(ly.num_held == kMaxHeld)
    {
        for(int k = 1; k < kMaxHeld; k++)
            ly.held[k - 1] = ly.held[k];
        ly.num_held--;
    }
    ly.held[ly.num_held++] = note;

    if(ly.patch.voice_mode == kPoly)
    {
        // The same note again takes its own voice back (with MPE, only
        // on the same channel).
        int index = -1;
        for(int i = 0; i < kVoices; i++)
        {
            const Voice &v = voices_[i];
            if(v.Layer() == layer && v.Active() && v.Note() == note
               && (!controllers_.mpe || v.Channel() == channel))
                index = i;
        }
        if(index < 0)
            index = Allocate();
        Play(layer, index, note, vel, true, held_before, channel);
    }
    else
        MonoTo(layer, note, vel, ly.patch.voice_mode == kMono || !held_before, held_before, channel);
}

void SynthEngine::LayerNoteOff(int layer, int note, float release_vel, int channel)
{
    Layer &ly = layers_[layer];
    Unhold(ly, note);

    if(ly.patch.voice_mode == kPoly)
    {
        if(sustain_)
        {
            sustained_[note] |= (uint8_t)(1 << layer);
            return;
        }
        for(Voice &v : voices_)
            if(v.Layer() == layer && v.Note() == note && !v.Released()
               && (!controllers_.mpe || v.Channel() == channel))
                v.Release(release_vel);
        return;
    }

    // Only letting go of the note sounding changes anything.
    int i = ly.mono_voice;
    if(i < 0 || voices_[i].Layer() != layer || voices_[i].Note() != note || voices_[i].Released())
        return;
    if(ly.num_held > 0)
    {
        // The highest key still down takes over.
        int highest = ly.held[0];
        for(int k = 1; k < ly.num_held; k++)
            if(ly.held[k] > highest)
                highest = ly.held[k];
        MonoTo(layer, highest, ly.velocity, ly.patch.voice_mode == kMono, true, ly.channel);
    }
    else if(sustain_)
        sustained_[note] |= (uint8_t)(1 << layer);
    else
        voices_[i].Release(release_vel);
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
        int layers = sustained_[n];
        if(layers == 0)
            continue;
        sustained_[n] = 0;
        for(int l = 0; l < kLayers; l++)
        {
            if(!(layers & (1 << l)) || Held(layers_[l], n))
                continue;
            for(Voice &v : voices_)
                if(v.Layer() == l && v.Note() == n && !v.Released())
                    v.Release();
        }
    }
}

void SynthEngine::LayerNotesOff(int layer)
{
    layers_[layer].num_held = 0;
    uint8_t bit             = (uint8_t)(1 << layer);
    for(int n = 0; n < 128; n++)
    {
        sustained_[n] &= (uint8_t)~bit;
        note_layers_[n] &= (uint8_t)~bit;
    }
    for(Voice &v : voices_)
        if(v.Layer() == layer)
            v.Release();
}

void SynthEngine::AllNotesOff()
{
    for(int l = 0; l < kLayers; l++)
        LayerNotesOff(l);
}

int SynthEngine::Routes() const
{
    int routes = 0;
    for(int l = 0; l < kLayers; l++)
        if(Plays(l))
            routes |= 1 << layers_[l].patch.routing;
    for(const Voice &v : voices_)
        if(v.Active())
            routes |= 1 << layers_[v.Layer()].patch.routing;
    return routes;
}

void SynthEngine::Process(float *const l[kNumFxRoutings], float *const r[kNumFxRoutings], size_t size)
{
    // Each layer's global LFOs: Free Global's speed follows the matrix of
    // the layer's voice last played; Clocked follows the tempo.
    for(Layer &ly : layers_)
    {
        Voice::Frame &f = ly.frame;
        for(int i = 0; i < 2; i++)
        {
            Lfo::Mode mode = f.lfo_mode[i];
            if(mode == Lfo::kFreeVoice)
                continue;
            if(mode == Lfo::kClocked && clock_restart_)
                ly.lfo[i].Restart();
            float rate;
            if(mode == Lfo::kClocked)
                rate = clock_beats_ / ly.lfo_beats[i];
            else
            {
                const int lv  = ly.last_voice;
                float     mod = lv >= 0 && voices_[lv].Layer() == (int)(&ly - layers_)
                                    ? voices_[lv].Mod((mod::Target)(mod::kLfo1Speed + i))
                                    : 0.0f;
                rate = f.lfo_rate[i] * dsp::FastExp2(mod);
            }
            f.global_lfo[i] = ly.lfo[i].Advance(rate, f.lfo_shape[i]);
        }
    }
    clock_restart_ = false;

    for(int b = 0; b < kNumFxRoutings; b++)
        for(size_t i = 0; i < size; i++)
            l[b][i] = r[b][i] = 0.0f;
    for(Voice &v : voices_)
        if(v.Active())
        {
            Layer &ly  = layers_[v.Layer()];
            int    bus = ly.patch.routing;
            v.Render(ly.frame, l[bus], r[bus], size);
        }
}

SynthEngine::Probe SynthEngine::GetProbe(int layer) const
{
    const Layer &ly = layers_[layer];
    Probe        p  = {};
    p.voices        = 0;
    for(const Voice &u : voices_)
        if(u.Active())
            p.voices++;
    if(ly.last_voice < 0 || voices_[ly.last_voice].Layer() != layer)
    {
        // Nothing played on this layer yet (or its voice has gone to
        // another): where its waves would start, silent.
        p.wave[0] = ly.frame.osc[0].wave;
        p.wave[1] = ly.frame.osc[1].wave;
        p.cutoff  = VoiceFilter::CutoffHz(ly.frame.cutoff);
        p.note    = -1;
        return p;
    }
    const Voice &v = voices_[ly.last_voice];
    p.wave[0] = v.WaveAt(0);
    p.wave[1] = v.WaveAt(1);
    p.cutoff  = v.CutoffHz();
    p.amp     = v.AmpEnv();
    p.note    = v.Note();
    p.drift   = v.DriftCents();
    p.lfo[0]  = v.LfoValue(0);
    p.lfo[1]  = v.LfoValue(1);
    return p;
}

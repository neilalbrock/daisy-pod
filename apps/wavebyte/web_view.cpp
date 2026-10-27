#include "web_view.h"

#include <cmath>

#include "presets.h"
#include "wavetables.h"

namespace web_view
{
namespace
{
using weblink::Kind;
using weblink::Store;

const char *const kVoiceModes[]  = {"Poly", "Mono", "Legato"};
const char *const kFilterTypes[] = {"LP 24", "LP 12", "HP", "Drive", "Off"};
const char *const kGlideModes[]  = {"Onset", "Legato"};
const char *const kEnvNames[]    = {"Wave", "Filter", "Amp"};
const char *const kPanModes[]    = {"Fix", "Rand 1", "Rand 2", "Rand 3", "Rand 4", "Rand 5"};
const char *const kOscModes[]    = {"8-bit", "Clean"};
const char *const kDirtTypes[]   = {"Static", "Crackle", "Geiger", "Click",
                                    "Burst 1", "Burst 2", "Burst 3", "Ring Mod"};
const char *const kGroupNames[]  = {"Osc 1", "Osc 2", "Pitch", "Filter", "Env & Glide",
                                    "Envelopes", "Amp", "Character", "Play"};
const char *const kMeterNames[]  = {"Out"};

const weblink::Trace kTraces[kNumTraces] = {
    {"Wave 1", "wave", 0, 63},
    {"Wave 2", "wave", 0, 63},
    {"Cutoff", "kHz", 0, 20},
    {"Amp", "%", 0, 100},
    {"Drift", "ct", -100, 100},
    {"Voices", "voices", 0, 8},
};

template <typename T, size_t N>
constexpr uint8_t Count(const T (&)[N])
{
    return (uint8_t)N;
}

constexpr Kind  kKnob   = Kind::kContinuous;
constexpr Store kPreset = Store::kPreset;
} // namespace

constexpr weblink::Param kParams[kNumParams] = {
    {"Wave 1", kKnob, 0, nullptr, 0, 0, kPreset},
    {"Env Amnt 1", kKnob, 0, nullptr, 0, 0, kPreset},
    {"Level 1", kKnob, 0, nullptr, 0, 0, kPreset},
    {"Vel 1", kKnob, 0, nullptr, 0, 0, kPreset},
    {"Wave 2", kKnob, 1, nullptr, 0, 0, kPreset},
    {"Env Amnt 2", kKnob, 1, nullptr, 0, 0, kPreset},
    {"Level 2", kKnob, 1, nullptr, 0, 0, kPreset},
    {"Vel 2", kKnob, 1, nullptr, 0, 0, kPreset},
    {"Semitone 2", kKnob, 2, nullptr, 0, 0, kPreset},
    {"Detune 2", kKnob, 2, nullptr, 0, 0, kPreset},
    {"Semitone 1", kKnob, 2, nullptr, 0, 0, kPreset},
    {"Detune 1", kKnob, 2, nullptr, 0, 0, kPreset},
    {"Cutoff", kKnob, 3, nullptr, 0, 0, kPreset},
    {"Resonance", kKnob, 3, nullptr, 0, 0, kPreset},
    {"Keytrack", kKnob, 3, nullptr, 0, 0, kPreset},
    {"Dirt", kKnob, 3, nullptr, 0, 0, kPreset},
    {"Filter Env", kKnob, 4, nullptr, 0, 0, kPreset},
    {"Glide", kKnob, 4, nullptr, 0, 0, kPreset},
    {"Filter Vel", kKnob, 4, nullptr, 0, 0, kPreset},
    {"Bend Range", kKnob, 4, nullptr, 0, 0, kPreset},
    {"Wave Attack", kKnob, 5, nullptr, 0, 0, kPreset},
    {"Wave Decay", kKnob, 5, nullptr, 0, 0, kPreset},
    {"Wave Sustain", kKnob, 5, nullptr, 0, 0, kPreset},
    {"Wave Release", kKnob, 5, nullptr, 0, 0, kPreset},
    {"Filter Attack", kKnob, 5, nullptr, 0, 0, kPreset},
    {"Filter Decay", kKnob, 5, nullptr, 0, 0, kPreset},
    {"Filter Sustain", kKnob, 5, nullptr, 0, 0, kPreset},
    {"Filter Release", kKnob, 5, nullptr, 0, 0, kPreset},
    {"Amp Attack", kKnob, 5, nullptr, 0, 0, kPreset},
    {"Amp Decay", kKnob, 5, nullptr, 0, 0, kPreset},
    {"Amp Sustain", kKnob, 5, nullptr, 0, 0, kPreset},
    {"Amp Release", kKnob, 5, nullptr, 0, 0, kPreset},
    {"Volume", kKnob, 6, nullptr, 0, 0, kPreset},
    {"Pan", kKnob, 6, nullptr, 0, 0, kPreset},
    {"Amp Vel", kKnob, 6, nullptr, 0, 0, kPreset},
    {"Master", kKnob, 6, nullptr, 0, 0, Store::kGlobal},
    {"Flavour", kKnob, 7, nullptr, 0, 0, kPreset},
    {"DAC", kKnob, 7, nullptr, 0, 0, kPreset},
    {"Table 1", Kind::kChoice, 0, Wavetables::kNames, Wavetables::kTables, 4, kPreset},
    {"Table 2", Kind::kChoice, 1, Wavetables::kNames, Wavetables::kTables, 4, kPreset},
    {"Voice", Kind::kChoice, 2, kVoiceModes, Count(kVoiceModes), 3, kPreset},
    {"Filter Type", Kind::kChoice, 3, kFilterTypes, Count(kFilterTypes), 5, kPreset},
    {"Glide Mode", Kind::kChoice, 4, kGlideModes, Count(kGlideModes), 2, kPreset},
    {"Envelope", Kind::kChoice, 5, kEnvNames, Count(kEnvNames), 3, Store::kNone},
    {"Pan Mode", Kind::kChoice, 6, kPanModes, Count(kPanModes), 6, kPreset},
    {"Osc Mode", Kind::kChoice, 7, kOscModes, Count(kOscModes), 2, kPreset},
    {"Dirt Type", Kind::kChoice, 3, kDirtTypes, Count(kDirtTypes), 4, kPreset},
    {"Limit Waves 1", Kind::kToggle, 0, nullptr, 0, 0, kPreset},
    {"Limit Waves 2", Kind::kToggle, 1, nullptr, 0, 0, kPreset},
    {"Fixed 1", Kind::kToggle, 2, nullptr, 0, 0, kPreset},
    {"Fixed 2", Kind::kToggle, 2, nullptr, 0, 0, kPreset},
    {"Play Note", kKnob, 8, nullptr, 0, 0, Store::kGlobal},
    {"Play", Kind::kToggle, 8, nullptr, 0, 0, Store::kNone},
};
static_assert(UniqueKeys(kParams, kNumParams), "two parameter names share a preset key");

void WriteHello(weblink::Writer &w, float sample_rate)
{
    weblink::Hello h  = {};
    h.app             = "wavebyte";
    h.sample_rate     = (uint32_t)sample_rate;
    h.groups          = kGroupNames;
    h.num_groups      = Count(kGroupNames);
    h.meters          = kMeterNames;
    h.num_meters      = Count(kMeterNames);
    h.num_params      = kNumParams;
    h.traces          = kTraces;
    h.num_traces      = kNumTraces;
    h.trace_period_ms = kTracePeriodMs;
    h.presets         = Presets::kSlots;
    weblink::WriteHello(w, h);
}

void WriteFrame(weblink::Writer &w, const Telemetry &t, float cpu_avg, float cpu_max)
{
    using weblink::Section;
    w.Begin(weblink::Msg::kFrame);

    w.U7((uint32_t)Section::kLevels);
    w.U7(1);
    w.U14((uint32_t)(fminf(t.peak, 1.0f) * 16383.0f));

    w.U7((uint32_t)Section::kCpu);
    w.U7((uint32_t)(cpu_avg * 100.0f + 0.5f));
    w.U7((uint32_t)(cpu_max * 100.0f + 0.5f));

    if(t.num_points > 0)
    {
        w.U7((uint32_t)Section::kTraces);
        w.U7(t.num_points);
        w.U7(kNumTraces);
        for(int i = 0; i < t.num_points; i++)
            for(int k = 0; k < kNumTraces; k++)
                weblink::WriteTraceValue(w, kTraces[k], t.trace[i][k]);
    }
}
} // namespace web_view

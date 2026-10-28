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
const char *const kLfoNames[]    = {"LFO 1", "LFO 2"};
const char *const kSlotNames[]   = {"1", "2", "3", "4", "5", "6", "7", "8"};
const char *const kFxSlots[]     = {"FX 1", "FX 2"};
const char *const kFxRoutings[]  = {"Dry", "FX 1 > FX 2", "FX 2 only"};
const char *const kLayerNames[]  = {"A", "B", "C", "D"};
const char *const kLayerModes[]  = {"Single", "Layered", "Round Robin", "Random Robin", "MIDI Split"};
const char *const kChannels[]    = {"1", "2", "3", "4", "5", "6", "7", "8",
                                    "9", "10", "11", "12", "13", "14", "15", "16"};

// Each layer's cards, as "<layer>/<card>": the page shows a layer's in a
// tab of its own. Then the shared ones: the effects each have a card,
// after the FX card's (kFxGroup + type). The hello carries every name, and
// must fit in one message (weblink::kMaxMessage, 720 bytes): with these
// it's 672, so keep new names short.
#define LAYER_GROUPS(L)                                                                     \
    L "/Osc 1", L "/Osc 2", L "/Pitch", L "/Filter", L "/Env & Glide", L "/Envelopes",     \
        L "/Amp", L "/Character", L "/LFOs", L "/Mod Matrix", L "/Layer"
constexpr uint8_t kLayerGroups  = 11;
const char *const kGroupNames[] = {LAYER_GROUPS("A"), LAYER_GROUPS("B"), LAYER_GROUPS("C"),
                                   LAYER_GROUPS("D"), "Global", "FX", "Chorus", "Flanger",
                                   "Phaser", "Tremolo", "Drive", "Compressor", "EQ", "Delay",
                                   "Reverb", "Pod Pages", "Play"};
#undef LAYER_GROUPS
constexpr uint8_t kGlobalGroup = kLayers * kLayerGroups;
constexpr uint8_t kFxGroup     = kGlobalGroup + 1;
constexpr uint8_t kPodGroup    = kFxGroup + kNumFxTypes;
constexpr uint8_t kPlayGroup   = kPodGroup + 1;
const char *const kMeterNames[]  = {"Out"};

const weblink::Trace kTraces[kNumTraces] = {
    {"Wave 1", "wave", 0, 63},
    {"Wave 2", "wave", 0, 63},
    {"Cutoff", "kHz", 0, 20},
    {"Amp", "%", 0, 100},
    {"Drift", "ct", -100, 100},
    {"LFO 1", "%", -100, 100},
    {"LFO 2", "%", -100, 100},
    {"Voices", "voices", 0, 8},
    {"Stack free", "KB", 0, 16},
};

template <typename T, size_t N>
constexpr uint8_t Count(const T (&)[N])
{
    return (uint8_t)N;
}

constexpr Kind  kKnob   = Kind::kContinuous;
constexpr Store kPreset = Store::kPreset;
} // namespace

// One layer's parameters, in ParamId's order: P is the name's prefix ("" for
// layer A, "B: " and so on), G the layer's first card.
#define MOD_SLOT(P, G, N)                                                                     \
    {P "Mod " N " Source", kKnob, G + 9, nullptr, 0, 0, kPreset},                             \
        {P "Mod " N " Amount", kKnob, G + 9, nullptr, 0, 0, kPreset},                         \
        {P "Mod " N " Target", kKnob, G + 9, nullptr, 0, 0, kPreset}
#define LAYER_PARAMS(P, G)                                                                    \
    {P "Wave 1", kKnob, G + 0, nullptr, 0, 0, kPreset},                                       \
        {P "Env Amnt 1", kKnob, G + 0, nullptr, 0, 0, kPreset},                               \
        {P "Level 1", kKnob, G + 0, nullptr, 0, 0, kPreset},                                  \
        {P "Vel 1", kKnob, G + 0, nullptr, 0, 0, kPreset},                                    \
        {P "Wave 2", kKnob, G + 1, nullptr, 0, 0, kPreset},                                   \
        {P "Env Amnt 2", kKnob, G + 1, nullptr, 0, 0, kPreset},                               \
        {P "Level 2", kKnob, G + 1, nullptr, 0, 0, kPreset},                                  \
        {P "Vel 2", kKnob, G + 1, nullptr, 0, 0, kPreset},                                    \
        {P "Semitone 2", kKnob, G + 2, nullptr, 0, 0, kPreset},                               \
        {P "Detune 2", kKnob, G + 2, nullptr, 0, 0, kPreset},                                 \
        {P "Semitone 1", kKnob, G + 2, nullptr, 0, 0, kPreset},                               \
        {P "Detune 1", kKnob, G + 2, nullptr, 0, 0, kPreset},                                 \
        {P "Cutoff", kKnob, G + 3, nullptr, 0, 0, kPreset},                                   \
        {P "Resonance", kKnob, G + 3, nullptr, 0, 0, kPreset},                                \
        {P "Keytrack", kKnob, G + 3, nullptr, 0, 0, kPreset},                                 \
        {P "Dirt", kKnob, G + 3, nullptr, 0, 0, kPreset},                                     \
        {P "Filter Env", kKnob, G + 4, nullptr, 0, 0, kPreset},                               \
        {P "Glide", kKnob, G + 4, nullptr, 0, 0, kPreset},                                    \
        {P "Filter Vel", kKnob, G + 4, nullptr, 0, 0, kPreset},                               \
        {P "Bend Range", kKnob, G + 4, nullptr, 0, 0, kPreset},                               \
        {P "Wave Attack", kKnob, G + 5, nullptr, 0, 0, kPreset},                              \
        {P "Wave Decay", kKnob, G + 5, nullptr, 0, 0, kPreset},                               \
        {P "Wave Sustain", kKnob, G + 5, nullptr, 0, 0, kPreset},                             \
        {P "Wave Release", kKnob, G + 5, nullptr, 0, 0, kPreset},                             \
        {P "Filter Attack", kKnob, G + 5, nullptr, 0, 0, kPreset},                            \
        {P "Filter Decay", kKnob, G + 5, nullptr, 0, 0, kPreset},                             \
        {P "Filter Sustain", kKnob, G + 5, nullptr, 0, 0, kPreset},                           \
        {P "Filter Release", kKnob, G + 5, nullptr, 0, 0, kPreset},                           \
        {P "Amp Attack", kKnob, G + 5, nullptr, 0, 0, kPreset},                               \
        {P "Amp Decay", kKnob, G + 5, nullptr, 0, 0, kPreset},                                \
        {P "Amp Sustain", kKnob, G + 5, nullptr, 0, 0, kPreset},                              \
        {P "Amp Release", kKnob, G + 5, nullptr, 0, 0, kPreset},                              \
        {P "Volume", kKnob, G + 6, nullptr, 0, 0, kPreset},                                   \
        {P "Pan", kKnob, G + 6, nullptr, 0, 0, kPreset},                                      \
        {P "Amp Vel", kKnob, G + 6, nullptr, 0, 0, kPreset},                                  \
        {P "LFO 1 Speed", kKnob, G + 8, nullptr, 0, 0, kPreset},                              \
        {P "LFO 1 Shape", kKnob, G + 8, nullptr, 0, 0, kPreset},                              \
        {P "LFO 1 Ratio", kKnob, G + 8, nullptr, 0, 0, kPreset},                              \
        {P "LFO 1 Mode", kKnob, G + 8, nullptr, 0, 0, kPreset},                               \
        {P "LFO 2 Speed", kKnob, G + 8, nullptr, 0, 0, kPreset},                              \
        {P "LFO 2 Shape", kKnob, G + 8, nullptr, 0, 0, kPreset},                              \
        {P "LFO 2 Ratio", kKnob, G + 8, nullptr, 0, 0, kPreset},                              \
        {P "LFO 2 Mode", kKnob, G + 8, nullptr, 0, 0, kPreset},                               \
        MOD_SLOT(P, G, "1"), MOD_SLOT(P, G, "2"), MOD_SLOT(P, G, "3"), MOD_SLOT(P, G, "4"),   \
        MOD_SLOT(P, G, "5"), MOD_SLOT(P, G, "6"), MOD_SLOT(P, G, "7"), MOD_SLOT(P, G, "8"),   \
        {P "Table 1", Kind::kChoice, G + 0, Wavetables::kNames, Wavetables::kTables, 4, kPreset}, \
        {P "Table 2", Kind::kChoice, G + 1, Wavetables::kNames, Wavetables::kTables, 4, kPreset}, \
        {P "Voice", Kind::kChoice, G + 2, kVoiceModes, Count(kVoiceModes), 3, kPreset},       \
        {P "Filter Type", Kind::kChoice, G + 3, kFilterTypes, Count(kFilterTypes), 5, kPreset}, \
        {P "Glide Mode", Kind::kChoice, G + 4, kGlideModes, Count(kGlideModes), 2, kPreset},  \
        {P "Pan Mode", Kind::kChoice, G + 6, kPanModes, Count(kPanModes), 6, kPreset},        \
        {P "Osc Mode", Kind::kChoice, G + 7, kOscModes, Count(kOscModes), 2, kPreset},        \
        {P "Dirt Type", Kind::kChoice, G + 3, kDirtTypes, Count(kDirtTypes), 4, kPreset},     \
        {P "FX Routing", Kind::kChoice, G + 10, kFxRoutings, Count(kFxRoutings), 3, kPreset}, \
        {P "Limit Waves 1", Kind::kToggle, G + 0, nullptr, 0, 0, kPreset},                    \
        {P "Limit Waves 2", Kind::kToggle, G + 1, nullptr, 0, 0, kPreset},                    \
        {P "Fixed 1", Kind::kToggle, G + 2, nullptr, 0, 0, kPreset},                          \
        {P "Fixed 2", Kind::kToggle, G + 2, nullptr, 0, 0, kPreset},                          \
        {P "Active", Kind::kToggle, G + 10, nullptr, 0, 0, kPreset}

constexpr weblink::Param kParams[kNumParams] = {
    LAYER_PARAMS("", 0),
    LAYER_PARAMS("B: ", kLayerGroups),
    LAYER_PARAMS("C: ", 2 * kLayerGroups),
    LAYER_PARAMS("D: ", 3 * kLayerGroups),
    {"Master", kKnob, kGlobalGroup, nullptr, 0, 0, Store::kGlobal},
    {"Flavour", kKnob, kGlobalGroup, nullptr, 0, 0, kPreset},
    {"DAC", kKnob, kGlobalGroup, nullptr, 0, 0, kPreset},
    {"FX 1 Type", kKnob, kFxGroup, nullptr, 0, 0, kPreset},
    {"FX 2 Type", kKnob, kFxGroup, nullptr, 0, 0, kPreset},
    {"Envelope", Kind::kChoice, kPodGroup, kEnvNames, Count(kEnvNames), 3, Store::kNone},
    {"LFO", Kind::kChoice, kPodGroup, kLfoNames, Count(kLfoNames), 2, Store::kNone},
    {"Mod Slot", Kind::kChoice, kPodGroup, kSlotNames, Count(kSlotNames), 8, Store::kNone},
    {"FX Slot", Kind::kChoice, kPodGroup, kFxSlots, Count(kFxSlots), 2, Store::kNone},
    {"Layer", Kind::kChoice, kGlobalGroup, kLayerNames, Count(kLayerNames), 4, kPreset},
    {"Layer Mode", Kind::kChoice, kGlobalGroup, kLayerModes, Count(kLayerModes), 3, kPreset},
    {"Split Channel", Kind::kChoice, kGlobalGroup, kChannels, Count(kChannels), 8, Store::kGlobal},
    {"A All Channels", Kind::kToggle, kGlobalGroup, nullptr, 0, 0, kPreset},
    {kFxParams[0].name, kKnob, kFxGroup + 1, nullptr, 0, 0, kPreset},
    {kFxParams[1].name, kKnob, kFxGroup + 1, nullptr, 0, 0, kPreset},
    {kFxParams[2].name, kKnob, kFxGroup + 1, nullptr, 0, 0, kPreset},
    {kFxParams[3].name, kKnob, kFxGroup + 1, nullptr, 0, 0, kPreset},
    {kFxParams[4].name, kKnob, kFxGroup + 1, nullptr, 0, 0, kPreset},
    {kFxParams[5].name, kKnob, kFxGroup + 1, nullptr, 0, 0, kPreset},
    {kFxParams[6].name, kKnob, kFxGroup + 1, nullptr, 0, 0, kPreset},
    {kFxParams[7].name, kKnob, kFxGroup + 1, nullptr, 0, 0, kPreset},
    {kFxParams[8].name, kKnob, kFxGroup + 1, nullptr, 0, 0, kPreset},
    {kFxParams[9].name, kKnob, kFxGroup + 1, nullptr, 0, 0, kPreset},
    {kFxParams[10].name, kKnob, kFxGroup + 2, nullptr, 0, 0, kPreset},
    {kFxParams[11].name, kKnob, kFxGroup + 2, nullptr, 0, 0, kPreset},
    {kFxParams[12].name, kKnob, kFxGroup + 2, nullptr, 0, 0, kPreset},
    {kFxParams[13].name, kKnob, kFxGroup + 2, nullptr, 0, 0, kPreset},
    {kFxParams[14].name, kKnob, kFxGroup + 2, nullptr, 0, 0, kPreset},
    {kFxParams[15].name, kKnob, kFxGroup + 2, nullptr, 0, 0, kPreset},
    {kFxParams[16].name, kKnob, kFxGroup + 2, nullptr, 0, 0, kPreset},
    {kFxParams[17].name, kKnob, kFxGroup + 3, nullptr, 0, 0, kPreset},
    {kFxParams[18].name, kKnob, kFxGroup + 3, nullptr, 0, 0, kPreset},
    {kFxParams[19].name, kKnob, kFxGroup + 3, nullptr, 0, 0, kPreset},
    {kFxParams[20].name, kKnob, kFxGroup + 3, nullptr, 0, 0, kPreset},
    {kFxParams[21].name, kKnob, kFxGroup + 3, nullptr, 0, 0, kPreset},
    {kFxParams[22].name, kKnob, kFxGroup + 3, nullptr, 0, 0, kPreset},
    {kFxParams[23].name, kKnob, kFxGroup + 3, nullptr, 0, 0, kPreset},
    {kFxParams[24].name, kKnob, kFxGroup + 3, nullptr, 0, 0, kPreset},
    {kFxParams[25].name, kKnob, kFxGroup + 3, nullptr, 0, 0, kPreset},
    {kFxParams[26].name, kKnob, kFxGroup + 3, nullptr, 0, 0, kPreset},
    {kFxParams[27].name, kKnob, kFxGroup + 4, nullptr, 0, 0, kPreset},
    {kFxParams[28].name, kKnob, kFxGroup + 4, nullptr, 0, 0, kPreset},
    {kFxParams[29].name, kKnob, kFxGroup + 4, nullptr, 0, 0, kPreset},
    {kFxParams[30].name, kKnob, kFxGroup + 4, nullptr, 0, 0, kPreset},
    {kFxParams[31].name, kKnob, kFxGroup + 4, nullptr, 0, 0, kPreset},
    {kFxParams[32].name, kKnob, kFxGroup + 5, nullptr, 0, 0, kPreset},
    {kFxParams[33].name, kKnob, kFxGroup + 5, nullptr, 0, 0, kPreset},
    {kFxParams[34].name, kKnob, kFxGroup + 5, nullptr, 0, 0, kPreset},
    {kFxParams[35].name, kKnob, kFxGroup + 5, nullptr, 0, 0, kPreset},
    {kFxParams[36].name, kKnob, kFxGroup + 6, nullptr, 0, 0, kPreset},
    {kFxParams[37].name, kKnob, kFxGroup + 6, nullptr, 0, 0, kPreset},
    {kFxParams[38].name, kKnob, kFxGroup + 6, nullptr, 0, 0, kPreset},
    {kFxParams[39].name, kKnob, kFxGroup + 6, nullptr, 0, 0, kPreset},
    {kFxParams[40].name, kKnob, kFxGroup + 6, nullptr, 0, 0, kPreset},
    {kFxParams[41].name, kKnob, kFxGroup + 6, nullptr, 0, 0, kPreset},
    {kFxParams[42].name, kKnob, kFxGroup + 6, nullptr, 0, 0, kPreset},
    {kFxParams[43].name, kKnob, kFxGroup + 6, nullptr, 0, 0, kPreset},
    {kFxParams[44].name, kKnob, kFxGroup + 7, nullptr, 0, 0, kPreset},
    {kFxParams[45].name, kKnob, kFxGroup + 7, nullptr, 0, 0, kPreset},
    {kFxParams[46].name, kKnob, kFxGroup + 7, nullptr, 0, 0, kPreset},
    {kFxParams[47].name, kKnob, kFxGroup + 7, nullptr, 0, 0, kPreset},
    {kFxParams[48].name, kKnob, kFxGroup + 7, nullptr, 0, 0, kPreset},
    {kFxParams[49].name, kKnob, kFxGroup + 7, nullptr, 0, 0, kPreset},
    {kFxParams[50].name, kKnob, kFxGroup + 7, nullptr, 0, 0, kPreset},
    {kFxParams[51].name, kKnob, kFxGroup + 7, nullptr, 0, 0, kPreset},
    {kFxParams[52].name, kKnob, kFxGroup + 7, nullptr, 0, 0, kPreset},
    {kFxParams[53].name, kKnob, kFxGroup + 7, nullptr, 0, 0, kPreset},
    {kFxParams[54].name, kKnob, kFxGroup + 7, nullptr, 0, 0, kPreset},
    {kFxParams[55].name, kKnob, kFxGroup + 7, nullptr, 0, 0, kPreset},
    {kFxParams[56].name, kKnob, kFxGroup + 8, nullptr, 0, 0, kPreset},
    {kFxParams[57].name, kKnob, kFxGroup + 8, nullptr, 0, 0, kPreset},
    {kFxParams[58].name, kKnob, kFxGroup + 8, nullptr, 0, 0, kPreset},
    {kFxParams[59].name, kKnob, kFxGroup + 8, nullptr, 0, 0, kPreset},
    {kFxParams[60].name, kKnob, kFxGroup + 8, nullptr, 0, 0, kPreset},
    {kFxParams[61].name, kKnob, kFxGroup + 8, nullptr, 0, 0, kPreset},
    {kFxParams[62].name, kKnob, kFxGroup + 8, nullptr, 0, 0, kPreset},
    {kFxParams[63].name, kKnob, kFxGroup + 8, nullptr, 0, 0, kPreset},
    {kFxParams[64].name, kKnob, kFxGroup + 8, nullptr, 0, 0, kPreset},
    {kFxParams[65].name, kKnob, kFxGroup + 8, nullptr, 0, 0, kPreset},
    {kFxParams[66].name, kKnob, kFxGroup + 9, nullptr, 0, 0, kPreset},
    {kFxParams[67].name, kKnob, kFxGroup + 9, nullptr, 0, 0, kPreset},
    {kFxParams[68].name, kKnob, kFxGroup + 9, nullptr, 0, 0, kPreset},
    {kFxParams[69].name, kKnob, kFxGroup + 9, nullptr, 0, 0, kPreset},
    {kFxParams[70].name, kKnob, kFxGroup + 9, nullptr, 0, 0, kPreset},
    {kFxParams[71].name, kKnob, kFxGroup + 9, nullptr, 0, 0, kPreset},
    {"Play Note", kKnob, kPlayGroup, nullptr, 0, 0, Store::kGlobal},
    {"Play", Kind::kToggle, kPlayGroup, nullptr, 0, 0, Store::kNone},
    {"MPE", Kind::kToggle, kPlayGroup, nullptr, 0, 0, Store::kGlobal},
    {"Tempo", kKnob, kPlayGroup, nullptr, 0, 0, Store::kGlobal},
};
#undef LAYER_PARAMS
#undef MOD_SLOT
// Too many entries won't compile; too few would leave the last unnamed.
constexpr bool AllNamed(const weblink::Param *params, int n)
{
    for(int i = 0; i < n; i++)
        if(params[i].name == nullptr)
            return false;
    return true;
}
static_assert(AllNamed(kParams, kNumParams), "the list is out of step with ParamId");
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

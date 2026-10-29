#include "web_view.h"

#include <cmath>

#include "presets.h"

namespace web_view
{
namespace
{
using weblink::Kind;
using weblink::Store;

const char *const kWidthNames[]    = {"Narrow", "Medium", "Wide"};
const char *const kPredelayNames[] = {"0 ms", "15 ms", "30 ms", "60 ms", "120 ms", "240 ms"};
const char *const kStrikeNames[]   = {"Flash", "Glint", "Fork"};
const char *const kGroupNames[]    = {"Page 1", "Page 2", "Page 3", "Page 4", "Page 5", "Page 6", "Page 7", "Switches"};
const char *const kMeterNames[]    = {"In", "Wet", "Out"};

const weblink::Trace kTraces[kNumTraces] = {
    {"Sea", "dB", -60, 0},
    {"Tide", "dB", -5, 5},
    {"Storm", "%", 0, 100},
    {"Flash", "%", 0, 100},
    {"Decay", "s", 0, (int16_t)kHeldDecay},
};

template <typename T, size_t N>
constexpr uint8_t Count(const T (&)[N])
{
    return (uint8_t)N;
}
} // namespace

constexpr weblink::Param kParams[kNumParams] = {
    {"Decay", Kind::kContinuous, 0, nullptr, 0, 0, Store::kPreset},
    {"Mix", Kind::kContinuous, 0, nullptr, 0, 0, Store::kPreset},
    {"Size", Kind::kContinuous, 1, nullptr, 0, 0, Store::kPreset},
    {"Tone", Kind::kContinuous, 1, nullptr, 0, 0, Store::kPreset},
    {"Depth", Kind::kContinuous, 2, nullptr, 0, 0, Store::kPreset},
    {"Tide", Kind::kContinuous, 2, nullptr, 0, 0, Store::kPreset},
    {"Storm", Kind::kContinuous, 3, nullptr, 0, 0, Store::kPreset},
    {"Gusts", Kind::kContinuous, 3, nullptr, 0, 0, Store::kPreset},
    {"Wind", Kind::kContinuous, 4, nullptr, 0, 0, Store::kPreset},
    {"Rain", Kind::kContinuous, 4, nullptr, 0, 0, Store::kPreset},
    {"Lightning", Kind::kContinuous, 5, nullptr, 0, 0, Store::kPreset},
    {"Thunder", Kind::kContinuous, 5, nullptr, 0, 0, Store::kPreset},
    {"Surge", Kind::kContinuous, 6, nullptr, 0, 0, Store::kPreset},
    {"Width", Kind::kChoice, 0, kWidthNames, Count(kWidthNames), 3, Store::kPreset},
    {"Predelay", Kind::kChoice, 1, kPredelayNames, Count(kPredelayNames), 3, Store::kPreset},
    {"Strike", Kind::kChoice, 5, kStrikeNames, Count(kStrikeNames), 3, Store::kPreset},
    {"Trails", Kind::kToggle, 7, nullptr, 0, 0, Store::kGlobal},
    {"Kill dry", Kind::kToggle, 7, nullptr, 0, 0, Store::kGlobal},
    {"On", Kind::kToggle, 7, nullptr, 0, 0, Store::kNone},
    {"Freeze", Kind::kToggle, 7, nullptr, 0, 0, Store::kNone},
    {"Calm", Kind::kToggle, 7, nullptr, 0, 0, Store::kNone},
    {"Squall", Kind::kToggle, 7, nullptr, 0, 0, Store::kNone},
    {"Mono in", Kind::kToggle, 7, nullptr, 0, 0, Store::kGlobal},
};
static_assert(UniqueKeys(kParams, kNumParams), "two parameter names share a preset key");

void WriteHello(weblink::Writer &w, float sample_rate)
{
    weblink::Hello h = {};
    h.app             = "stormsea";
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
    w.U7(3);
    for(float peak : t.peaks)
        w.U14((uint32_t)(fminf(peak, 1.0f) * 16383.0f));

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

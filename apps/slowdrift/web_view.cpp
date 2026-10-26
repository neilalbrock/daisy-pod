#include "web_view.h"

#include <cmath>

namespace web_view
{
namespace
{
using weblink::Kind;

const char *const kModeNames[]   = {"Dream", "Light", "Rain", "Dark", "Rise"};
const char *const kShapeNames[]  = {"Sine", "Warp", "Sink", "Square", "Random"};
const char *const kWidthNames[]  = {"Narrow", "Medium", "Wide"};
const char *const kGroupNames[]  = {"Page 1", "Page 2", "Page 3", "Page 4", "Switches"};
const char *const kMeterNames[]  = {"In", "Wet", "Out"};

const weblink::Trace kTraces[kNumTraces] = {
    {"LFO", "%", -100, 100},
    {"Input", "%", 0, 100},
    {"Decay", "s", 0, 60},
};

template <typename T, size_t N>
constexpr uint8_t Count(const T (&)[N])
{
    return (uint8_t)N;
}
} // namespace

const weblink::Param kParams[kNumParams] = {
    {"Decay", Kind::kContinuous, 0, nullptr, 0, 0},
    {"Mix", Kind::kContinuous, 0, nullptr, 0, 0},
    {"Filter", Kind::kContinuous, 1, nullptr, 0, 0},
    {"Depth", Kind::kContinuous, 1, nullptr, 0, 0},
    {"X", Kind::kContinuous, 2, nullptr, 0, 0},
    {"Stretch", Kind::kContinuous, 2, nullptr, 0, 0},
    {"Rate", Kind::kContinuous, 3, nullptr, 0, 0},
    {"Pan", Kind::kContinuous, 3, nullptr, 0, 0},
    {"Mode", Kind::kChoice, 0, kModeNames, Count(kModeNames), 5},
    {"Waveshape", Kind::kChoice, 1, kShapeNames, Count(kShapeNames), 5},
    {"Width", Kind::kChoice, 2, kWidthNames, Count(kWidthNames), 3},
    {"Trails", Kind::kToggle, 3, nullptr, 0, 0},
    {"On", Kind::kToggle, 4, nullptr, 0, 0},
    {"Sustain", Kind::kToggle, 4, nullptr, 0, 0},
    {"Freeze", Kind::kToggle, 4, nullptr, 0, 0},
    {"Mono in", Kind::kToggle, 4, nullptr, 0, 0},
};

void WriteHello(weblink::Writer &w, float sample_rate)
{
    weblink::Hello h = {};
    h.app             = "slowdrift";
    h.sample_rate     = (uint32_t)sample_rate;
    h.groups          = kGroupNames;
    h.num_groups      = Count(kGroupNames);
    h.meters          = kMeterNames;
    h.num_meters      = Count(kMeterNames);
    h.num_params      = kNumParams;
    h.traces          = kTraces;
    h.num_traces      = kNumTraces;
    h.trace_period_ms = kTracePeriodMs;
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

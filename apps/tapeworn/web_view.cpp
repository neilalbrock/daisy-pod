#include "web_view.h"

#include <cmath>

#include "tape_engine.h"

namespace web_view
{
namespace
{
using weblink::Kind;

// Filled from TapeModel::Name() by WriteHello(), before any parameter goes
// out.
const char *model_names[TapeModel::kNumModels];

const char *const kAuxNames[]   = {"Stop", "Fail", "Filter"};
const char *const kGroupNames[] = {"Tape model", "Page 1", "Page 2", "Page 3",
                                   "Page 4",     "AUX",    "Switches"};
const char *const kMeterNames[] = {"In", "Wet", "Out"};

const weblink::Trace kTraces[kNumTraces] = {
    {"Speed L", "cents", -200, 200},
    {"Speed R", "cents", -200, 200},
    {"Level L", "%", 0, 100},
    {"Level R", "%", 0, 100},
};

template <typename T, size_t N>
constexpr uint8_t Count(const T (&)[N])
{
    return (uint8_t)N;
}
} // namespace

const weblink::Param kParams[kNumParams] = {
    {"Model", Kind::kChoice, 0, model_names, TapeModel::kNumModels, 1},
    {"Wow", Kind::kContinuous, 1, nullptr, 0, 0},
    {"Flutter", Kind::kContinuous, 1, nullptr, 0, 0},
    {"Saturate", Kind::kContinuous, 2, nullptr, 0, 0},
    {"Failure", Kind::kContinuous, 2, nullptr, 0, 0},
    {"Dry", Kind::kContinuous, 3, nullptr, 0, 0},
    {"Volume", Kind::kContinuous, 3, nullptr, 0, 0},
    {"Noise", Kind::kContinuous, 4, nullptr, 0, 0},
    {"Spread", Kind::kContinuous, 4, nullptr, 0, 0},
    {"AUX type", Kind::kChoice, 5, kAuxNames, Count(kAuxNames), 3},
    {"AUX", Kind::kToggle, 5, nullptr, 0, 0},
    {"Bypass", Kind::kToggle, 6, nullptr, 0, 0},
    {"Mono in", Kind::kToggle, 6, nullptr, 0, 0},
};

float SpeedCents(float speed)
{
    return 1200.0f * log2f(speed > 0.001f ? speed : 0.001f);
}

void WriteHello(weblink::Writer &w, float sample_rate)
{
    for(int i = 0; i < TapeModel::kNumModels; i++)
        model_names[i] = TapeModel::Name(i);

    weblink::Hello h = {};
    h.app             = "tapeworn";
    h.sample_rate     = (uint32_t)sample_rate;
    h.groups          = kGroupNames;
    h.num_groups      = Count(kGroupNames);
    h.meters          = kMeterNames;
    h.num_meters      = Count(kMeterNames);
    h.num_params      = kNumParams;
    h.traces          = kTraces;
    h.num_traces      = kNumTraces;
    h.trace_period_ms = kTracePeriodMs;
    h.xy              = "Saturation";
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

    // Each axis scaled to fill the plot, so the loop's shape shows at any
    // level; nothing while it's silent.
    float peak_in = 0.0f, peak_out = 0.0f;
    for(const auto &p : t.xy)
    {
        peak_in  = fmaxf(peak_in, fabsf(p[0]));
        peak_out = fmaxf(peak_out, fabsf(p[1]));
    }
    if(peak_in > 0.001f && peak_out > 0.001f)
    {
        w.U7((uint32_t)Section::kXY);
        w.U7(kXYPoints);
        for(const auto &p : t.xy)
            weblink::WriteXY(w, p[0] / peak_in, p[1] / peak_out);
    }
}
} // namespace web_view

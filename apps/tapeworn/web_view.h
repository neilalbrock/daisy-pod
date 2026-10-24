// Tapeworn - what the web page (web/) sees
//
// The parameter list in the page's order, and the hello and telemetry
// frame messages (common/web_link.h). Frames carry levels, CPU, traces of
// each channel's tape speed and level (wow, flutter, snags, drops and the
// tape stop), and the saturator's input against its output, which draws
// the hysteresis loop. main.cpp reads and sets the values, collects the
// Telemetry in the audio callback and sends the messages. libDaisy-free.

#pragma once

#include <cstddef>
#include <cstdint>

#include "web_link.h"

namespace web_view
{
// Wow to Spread are the Pod's knob pages in order: page (i - 1) / 2, knob
// (i - 1) % 2.
enum ParamId
{
    kParamModel,
    kParamWow,
    kParamFlutter,
    kParamSaturate,
    kParamFailure,
    kParamDry,
    kParamVolume,
    kParamNoise,
    kParamSpread,
    kParamAuxType,
    kParamAux,
    kParamBypass,
    kParamMono,
    kNumParams,
};

extern const weblink::Param kParams[kNumParams];

enum TraceId
{
    kTraceSpeedL,
    kTraceSpeedR,
    kTraceLevelL,
    kTraceLevelR,
    kNumTraces,
};

constexpr int kTracePeriodMs = 1;   // a trace point per ms
constexpr int kTracePoints   = 48;  // the most a frame carries (older ones are dropped)
constexpr int kXYPoints      = 96;  // saturator samples, every 4th (8 ms)

// What a frame shows: copied in the main loop from what the audio callback
// collected.
struct Telemetry
{
    float peaks[3]; // in, wet, out since the last frame
    float trace[kTracePoints][kNumTraces];
    int   num_points;
    float xy[kXYPoints][2]; // saturator in, out
};

// Speed (1 = normal) in cents for the speed traces.
float SpeedCents(float speed);

void WriteHello(weblink::Writer &w, float sample_rate);

void WriteFrame(weblink::Writer &w, const Telemetry &t, float cpu_avg, float cpu_max);
} // namespace web_view

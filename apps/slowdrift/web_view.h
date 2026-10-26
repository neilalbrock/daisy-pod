// Slowdrift - what the web page (web/) sees
//
// The parameter list in the page's order, and the hello and telemetry
// frame messages (common/web_link.h). Frames carry levels, CPU, and traces
// of the LFO, the reverb's input gain (Rise's swells, the bypass and freeze
// gate) and the decay time (Sustain, Freeze and Stretch stretching it).
// main.cpp reads and sets the values, collects the Telemetry in the audio
// callback and sends the messages. libDaisy-free.

#pragma once

#include <cstddef>
#include <cstdint>

#include "web_link.h"

namespace web_view
{
// Decay to Pan are the Pod's knob pages in order: page i / 2, knob i % 2.
// Mode, Waveshape, Width and Trails are the pages' encoder choices.
enum ParamId
{
    kParamDecay,
    kParamMix,
    kParamFilter,
    kParamDepth,
    kParamX,
    kParamStretch,
    kParamRate,
    kParamPan,
    kParamMode,
    kParamShape,
    kParamWidth,
    kParamTrails,
    kParamActive,
    kParamSustain,
    kParamFreeze,
    kParamMono,
    kNumParams,
};
constexpr int kNumKnobParams = kParamPan + 1;

extern const weblink::Param kParams[kNumParams];

enum TraceId
{
    kTraceLfo,
    kTraceInput,
    kTraceDecay,
    kNumTraces,
};

constexpr int kTracePeriodMs = 1;  // a trace point per ms
constexpr int kTracePoints   = 48; // the most a frame carries (older ones are dropped)

// What a frame shows: copied in the main loop from what the audio callback
// collected.
struct Telemetry
{
    float peaks[3]; // in, wet, out since the last frame
    float trace[kTracePoints][kNumTraces];
    int   num_points;
};

void WriteHello(weblink::Writer &w, float sample_rate);

void WriteFrame(weblink::Writer &w, const Telemetry &t, float cpu_avg, float cpu_max);
} // namespace web_view

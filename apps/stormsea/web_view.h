// Stormsea - what the web page (web/) sees
//
// The parameter list in the page's order, and the hello and telemetry
// frame messages (common/web_link.h). Frames carry levels, CPU, and traces
// of the sea's level, Tide's swell and the decay time (∞ while the sea is
// held). main.cpp reads and sets the values, collects the Telemetry in the
// audio callback and sends the messages. libDaisy-free.

#pragma once

#include <cstddef>
#include <cstdint>

#include "web_link.h"

namespace web_view
{
// Decay to Tide are the Pod's knob pages in order: page i / 2, knob i % 2.
// Width and Predelay are pages 1 and 2's encoder choices.
enum ParamId
{
    kParamDecay,
    kParamMix,
    kParamSize,
    kParamTone,
    kParamDepth,
    kParamTide,
    kParamWidth,
    kParamPredelay,
    kParamTrails,
    kParamKillDry,
    kParamActive,
    kParamFreeze,
    kParamMono,
    kNumParams,
};
constexpr int kNumKnobParams = kParamTide + 1;

extern const weblink::Param kParams[kNumParams];

enum TraceId
{
    kTraceSea,
    kTraceTide,
    kTraceDecay,
    kNumTraces,
};

constexpr float kHeldDecay = 999.0f; // the Decay trace while the sea is held: ∞ on the page

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

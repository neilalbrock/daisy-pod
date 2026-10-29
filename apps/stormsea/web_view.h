// Stormsea - what the web page (web/) sees
//
// The parameter list in the page's order, and the hello and telemetry
// frame messages (common/web_link.h). Frames carry levels, CPU, and traces
// of the sea's level, Tide's swell, the storm's strength, lightning's
// flashes, wind, rain and thunder, and the decay time (∞ while the sea is
// held): the page draws the sea from them (web/js/views/sea.js). main.cpp reads and sets the values, collects the Telemetry in the
// audio callback and sends the messages. libDaisy-free.

#pragma once

#include <cstddef>
#include <cstdint>

#include "web_link.h"

namespace web_view
{
// Decay to Undertow are the Pod's knob pages in order: page i / 2, knob
// i % 2. Width, Predelay and Rumble are pages 1, 2 and 6's encoder
// choices.
enum ParamId
{
    kParamDecay,
    kParamMix,
    kParamSize,
    kParamTone,
    kParamDepth,
    kParamTide,
    kParamStorm,
    kParamGusts,
    kParamWind,
    kParamRain,
    kParamLightning,
    kParamDistance,
    kParamSurge,
    kParamUndertow,
    kParamWidth,
    kParamPredelay,
    kParamRumble,
    kParamTrails,
    kParamKillDry,
    kParamActive,
    kParamFreeze,
    kParamCalm,
    kParamSquall,
    kParamMono,
    kNumParams,
};
constexpr int kNumKnobParams = kParamUndertow + 1;

extern const weblink::Param kParams[kNumParams];

enum TraceId
{
    kTraceSea,
    kTraceTide,
    kTraceStorm,
    kTraceFlash,
    kTraceWind,
    kTraceRain,
    kTraceThunder,
    kTraceDecay,
    kNumTraces,
};

constexpr float kHeldDecay = 999.0f; // the Decay trace while the sea is held: ∞ on the page

// A point every 2 ms, so a frame of 8 traces stays well under a message.
constexpr int kTracePeriodMs = 2;
constexpr int kTracePoints   = 32; // the most a frame carries (older ones are dropped)

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

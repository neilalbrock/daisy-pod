// Wavebyte - what the web page (web/) sees
//
// The parameter list in the page's order, and the hello and telemetry
// frame messages (common/web_link.h). Frames carry the output level, CPU,
// and traces of the last voice played: both oscillators' wave positions,
// its cutoff and amp envelope, and how many voices are sounding. main.cpp
// reads and sets the values, collects the Telemetry in the audio callback
// and sends the messages. libDaisy-free.

#pragma once

#include <cstddef>
#include <cstdint>

#include "web_link.h"

namespace web_view
{
// The knob parameters come first, in pairs: the Pod's knob layers in
// order, layer i / 2, knob i % 2 (see main.cpp). Each page has a layer, a
// second under Shift, and the envelope page one pair per envelope.
enum ParamId
{
    kParamWave1,
    kParamEnvAmt1,
    kParamLevel1,
    kParamVel1,
    kParamWave2,
    kParamEnvAmt2,
    kParamLevel2,
    kParamVel2,
    kParamSemitone2,
    kParamDetune2,
    kParamSemitone1,
    kParamDetune1,
    kParamCutoff,
    kParamResonance,
    kParamTrack,
    kParamDirt,
    kParamFilterEnv,
    kParamGlide,
    kParamFilterVel,
    kParamBend,
    kParamWaveAttack,
    kParamWaveDecay,
    kParamWaveSustain,
    kParamWaveRelease,
    kParamFilterAttack,
    kParamFilterDecay,
    kParamFilterSustain,
    kParamFilterRelease,
    kParamAmpAttack,
    kParamAmpDecay,
    kParamAmpSustain,
    kParamAmpRelease,
    kParamVolume,
    kParamPan,
    kParamAmpVel,
    kParamMaster,
    // The pages' encoder choices.
    kParamTable1,
    kParamTable2,
    kParamVoiceMode,
    kParamFilterType,
    kParamGlideMode,
    kParamEnvSelect,
    kParamPanMode,
    // The web page's alone.
    kParamLimit1,
    kParamLimit2,
    kParamFixed1,
    kParamFixed2,
    kParamPlayNote,
    kParamPlay,
    kNumParams,
};
constexpr int kNumKnobParams = kParamMaster + 1;
constexpr int kFirstChoice   = kParamTable1;

extern const weblink::Param kParams[kNumParams];

enum TraceId
{
    kTraceWave1,
    kTraceWave2,
    kTraceCutoff,
    kTraceAmp,
    kTraceVoices,
    kNumTraces,
};

constexpr int kTracePeriodMs = 2;  // a trace point every 2 ms
constexpr int kTracePoints   = 24; // the most a frame carries (older ones are dropped)

// What a frame shows: copied in the main loop from what the audio callback
// collected.
struct Telemetry
{
    float peak; // out, since the last frame
    float trace[kTracePoints][kNumTraces];
    int   num_points;
};

void WriteHello(weblink::Writer &w, float sample_rate);

void WriteFrame(weblink::Writer &w, const Telemetry &t, float cpu_avg, float cpu_max);
} // namespace web_view

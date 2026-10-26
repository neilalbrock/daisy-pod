// Driftnet - what the web page (web/) sees
//
// The parameter list in the page's order, and the hello and telemetry
// frame messages (common/web_link.h). Frames carry levels, CPU, the tempo
// while synced, the loop (the ring, the loop in it and what's playing it),
// an overview of the ring, and traces of the clock, Env's detector and the
// ramp. The page draws the loop with web/js/views/loop.js. main.cpp reads
// and sets the values, collects the Telemetry in the audio callback and
// sends the messages. libDaisy-free.

#pragma once

#include <cstddef>
#include <cstdint>

#include "micro_looper.h"
#include "web_link.h"

namespace web_view
{
// Time to Mix are the Pod's knob pages in order: page i / 2, knob i % 2.
enum ParamId
{
    kParamTime,
    kParamWetModify,
    kParamLength,
    kParamLoopModify,
    kParamClock,
    kParamMix,
    kParamWetMode,
    kParamLoopMode,
    kParamRouting,
    // Channels
    kParamWetOn,
    kParamFreeze,
    kParamLooper,
    kParamOverdub,
    // Hidden options
    kParamTone,
    kParamWidth,
    kParamDirect,
    kParamFade,
    kParamBalance,
    kParamSync,
    kParamSolo,
    kParamLoopLength,
    // DIP switches (right bank)
    kParamClassic,
    kParamMiso,
    kParamSpread,
    kParamDryKill,
    kParamTrails,
    kParamLatch,
    kParamNoDub,
    kParamSmooth,
    // Ramping (left bank, and its settings)
    kParamRampTime,
    kParamRampWetModify,
    kParamRampClock,
    kParamRampLoopModify,
    kParamRampLength,
    kParamBounce,
    kParamSweep,
    kParamPolarity,
    kParamRampSpeed,
    kParamWaveform,
    kParamRamping,
    kParamExpression,
    kParamExpressionOn,
    // Tempo
    kParamTap,
    kParamTempo,
    kParamFollowClock,
    kParamSynced,
    // Synth Mode
    kParamSynth,
    kParamOutput,
    kParamOctave,
    kParamAttack,
    kParamDecay,
    kParamSustain,
    kParamRelease,
    kParamPortamento,
    kNumParams,
};

extern const weblink::Param kParams[kNumParams];

constexpr float kMinBpm = 40.0f, kBpmRange = 200.0f; // the Tempo parameter

enum TraceId
{
    kTraceClock,
    kTraceInput,
    kTraceThreshold,
    kTraceRamp,
    kNumTraces,
};

constexpr int kTracePeriodMs = 10;
constexpr int kTracePoints   = 8; // the most a frame carries

constexpr size_t kOverviewColumns = 1024; // over the loop's ring

// What a frame shows: copied in the audio callback when the main loop asks.
struct Telemetry
{
    float             peaks[4]; // in, wet, loop, out since the last frame
    MicroLooper::View loop;
    float             clock_hz;
    bool              synced, following;
    float             beat, samples_per_beat;
    float             trace[kTracePoints][kNumTraces];
    int               num_points;
};

void WriteHello(weblink::Writer &w, float sample_rate, uint32_t ring_frames);

// The overview columns sent so far, so each frame sends the next ones.
struct OverviewCursor
{
    uint32_t next;
};

// overview: a peak per column of the ring, refreshed by the main loop. The
// ring changes anywhere (overdubs), so frames send it round and round.
void WriteFrame(weblink::Writer  &w,
                const Telemetry  &t,
                float             sample_rate,
                uint32_t          ring_frames,
                float             cpu_avg,
                float             cpu_max,
                const uint8_t    *overview,
                OverviewCursor   *cursor);
} // namespace web_view

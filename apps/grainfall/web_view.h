// Grainfall - what the web page (web/) sees
//
// The parameter list in the page's order, and the hello and telemetry
// frame messages (common/web_link.h). main.cpp reads and sets the values,
// fills the Telemetry from the audio callback and sends the messages.
// libDaisy-free, so a host test can produce the same messages.

#pragma once

#include <cstddef>
#include <cstdint>

#include "grain_engine.h"
#include "web_link.h"

namespace web_view
{
// Activity to Volume are the Pod's knob pages in order: page (i - 1) / 2,
// knob (i - 1) % 2.
enum ParamId
{
    kParamEffect,
    kParamActivity,
    kParamRepeats,
    kParamShape,
    kParamTime,
    kParamFilter,
    kParamMix,
    kParamModDepth,
    kParamModRate,
    kParamResonance,
    kParamVolume,
    kParamTempo,
    kParamTap,
    kParamReverse,
    kParamHold,
    kParamBypass,
    kParamMono,
    kNumParams,
};

extern const weblink::Param kParams[kNumParams];

constexpr float kMinBpm = 40.0f, kBpmRange = 200.0f; // CC 10's range

// One moment of the engine, taken in the audio callback.
struct Telemetry
{
    weblink::Head heads[GrainEngine::kMaxHeads];
    int           num_heads;
    uint32_t      onsets[OnsetLog::kSize];
    int           num_onsets;
    uint32_t      write_pos;
    uint32_t      buffer_frames;
    float         peaks[3]; // in, wet, out since the last frame
    float         beat, samples_per_beat, steps_per_beat;
    bool          following;
};

// Fills t from the engine (the peaks and tempo are the caller's).
void Snapshot(const GrainEngine &engine, Telemetry *t);

void WriteHello(weblink::Writer &w,
                float            sample_rate,
                uint32_t         buffer_frames,
                size_t           overview_columns);

// The overview columns sent so far, so each frame sends the new ones.
struct OverviewCursor
{
    uint32_t next; // the next column to send
};

// restart: send the whole overview again, a slice a frame (a new page).
void WriteFrame(weblink::Writer &w,
                const Telemetry &t,
                float            sample_rate,
                float            cpu_avg,
                float            cpu_max,
                const uint8_t   *overview,
                size_t           overview_columns,
                bool             restart,
                OverviewCursor  *cursor);
} // namespace web_view

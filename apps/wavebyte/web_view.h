// Wavebyte - what the web page (web/) sees
//
// The parameter list in the page's order, and the hello and telemetry
// frame messages (common/web_link.h). Frames carry the output level, CPU,
// and traces of the last voice played: both oscillators' wave positions,
// its cutoff, amp envelope, Flavour's pitch drift and both LFOs, and how
// many voices are sounding. main.cpp
// reads and sets the values, collects the Telemetry in the audio callback
// and sends the messages. libDaisy-free.

#pragma once

#include <cstddef>
#include <cstdint>

#include "fx_params.h"
#include "web_link.h"

namespace web_view
{
// The knob parameters come first, in the Pod's order of knob layers (see
// main.cpp's KnobOf): up to the mod matrix, in pairs, layer i / 2, knob
// i % 2. Each page has a layer, a second under Shift, the envelope and LFO
// pages a pair per envelope or LFO, and the Character page one. The mod
// matrix has three per slot: Source and Amount, then Target under Shift
// (where knob 2 is Amount again). The FX page's only knob parameters are
// the slots' types: its other knobs show the effects' values (kParamFxFirst
// on), which have no knob of their own.
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
    kParamFlavour,
    kParamDac,
    kParamLfo1Speed,
    kParamLfo1Shape,
    kParamLfo1Ratio,
    kParamLfo1Mode,
    kParamLfo2Speed,
    kParamLfo2Shape,
    kParamLfo2Ratio,
    kParamLfo2Mode,
    kParamMod1Source, // then Amount and Target, for each of the 8 slots
    kParamLastMod = kParamMod1Source + 3 * 8 - 1,
    kParamFx1Type,
    kParamFx2Type,
    // The pages' encoder choices.
    kParamTable1,
    kParamTable2,
    kParamVoiceMode,
    kParamFilterType,
    kParamGlideMode,
    kParamEnvSelect,
    kParamPanMode,
    kParamOscMode,
    kParamLfoSelect,
    kParamModSlot,
    kParamFxSlot,
    // The Filter and FX pages' encoder turns with Shift.
    kParamDirtType,
    kParamFxRouting,
    // The effects' values, by type (fx_params.h).
    kParamFxFirst,
    kParamFxLast = kParamFxFirst + kNumFxParams - 1,
    // The web page's alone.
    kParamLimit1,
    kParamLimit2,
    kParamFixed1,
    kParamFixed2,
    kParamPlayNote,
    kParamPlay,
    kParamMpe,
    kParamTempo,
    kNumParams,
};
constexpr int kNumKnobParams = kParamFx2Type + 1;
constexpr int kFirstChoice   = kParamTable1;

extern const weblink::Param kParams[kNumParams];

enum TraceId
{
    kTraceWave1,
    kTraceWave2,
    kTraceCutoff,
    kTraceAmp,
    kTraceDrift,
    kTraceLfo1,
    kTraceLfo2,
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

// Wavebyte - what the web page (web/) sees
//
// The parameter list in the page's order, and the hello and telemetry
// frame messages (common/web_link.h). Frames carry the output level, CPU,
// and traces of the last voice played: both oscillators' wave positions,
// its cutoff, amp envelope, Flavour's pitch drift and both LFOs, how
// many voices are sounding, and how much stack is to spare. main.cpp
// reads and sets the values, collects the Telemetry in the audio callback
// and sends the messages. libDaisy-free.

#pragma once

#include <cstddef>
#include <cstdint>

#include "fx_params.h"
#include "web_link.h"

namespace web_view
{
// The parameters come in two parts: each layer's, four times over (layer
// A's, then B's...), then those the layers share. A layer's start with its
// knob parameters, in the Pod's order of knob layers (see main.cpp's
// KnobOf): up to the LFOs in pairs, a layer and its Shift layer per page,
// the envelope and LFO pages a pair per envelope or LFO. The mod matrix
// has three per slot: Source and Amount, then Target under Shift (where
// knob 2 is Amount again). Then the layer's choices and switches.
//
// Layer A's names are the ones wavebyte had before it had layers, so
// presets saved then come back as layer A; B-D's put the layer first ("B:
// Cutoff": with a plain space, two pairs of names hash alike). LayerParam()
// gives a layer's index of one of its parameters.
constexpr int kLayers = 4;

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
    kParamAmpVel, // its knob 2 shows Master, which is shared
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
    kNumLayerKnobs,
    // The layer's pages' encoder choices.
    kParamTable1 = kNumLayerKnobs,
    kParamTable2,
    kParamVoiceMode,
    kParamFilterType,
    kParamGlideMode,
    kParamPanMode,
    kParamOscMode,
    // The Osc pages' encoder turns with Shift: a table from the AKWF bank
    // (0: off, the Table plays), else 1 + its number in akwf_bank.h.
    kParamAkwf1,
    kParamAkwf2,
    // The Filter and FX pages' encoder turns with Shift.
    kParamDirtType,
    kParamFxRouting,
    // The web page's alone.
    kParamLimit1,
    kParamLimit2,
    kParamFixed1,
    kParamFixed2,
    // In the Multi modes: whether the layer plays.
    kParamActive,
    kLayerParams,

    // Shared by the layers.
    kParamMaster = kLayers * kLayerParams,
    kParamFlavour,
    kParamDac,
    kParamFx1Type,
    kParamFx2Type,
    // What the Pod's pages show: the envelope, LFO, mod slot and FX slot.
    kParamEnvSelect,
    kParamLfoSelect,
    kParamModSlot,
    kParamFxSlot,
    // The Layer page: the layer edited (and played in Single), and the mode.
    kParamLayer,
    kParamLayerMode,
    // MIDI Split: layer A's channel (B-D the next ones up).
    kParamSplitChannel,
    // The edited layer's actions, as the Protein's Layer Action menu: back
    // to the starting sound, copy, paste, and load a layer of a stored
    // preset (the slot, then which of its layers).
    kParamInitLayer,
    kParamCopyLayer,
    kParamPasteLayer,
    kParamLoadSlot,
    kParamLoadFrom,
    kParamLoadLayer,
    // The effects' values, by type (fx_params.h).
    kParamFxFirst,
    kParamFxLast = kParamFxFirst + kNumFxParams - 1,
    // The web page's alone.
    kParamPlayNote,
    kParamPlay,
    kParamMpe,
    kParamTempo,
    kNumParams,
};

constexpr int LayerParam(int layer, int param)
{
    return layer * kLayerParams + param;
}

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
    kTraceStack, // KB of stack never used: the margin above the globals
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

// Wavebyte - the effects' types and parameters
//
// As on the Protein, each type can be in one slot at a time, so the
// parameters belong to the type, not the slot: one flat list, each type's
// in a run (kFxFirst, kFxCount). Values are 0..1, as a knob sets them; a
// parameter with steps picks from a list of that many (SynthEngine::Pick).
// The first of each type's is what the FX page's Amount knob sets.
//
// fx.cpp maps the values to units; web/js/apps/wavebyte.js shows the same.
// libDaisy-free.

#pragma once

#include <cstdint>

enum FxType : uint8_t
{
    kFxOff,
    kFxChorus,
    kFxFlanger,
    kFxPhaser,
    kFxTremolo,
    kFxDrive,
    kFxCompressor,
    kFxEq,
    kFxDelay,
    kFxReverb,
    kNumFxTypes,
};

enum FxRouting : uint8_t
{
    kFxDry,      // no effects
    kFxSerial,   // FX 1, then FX 2
    kFxOnlyTwo,  // FX 2 only
    kNumFxRoutings,
};

struct FxParam
{
    const char *name;   // also its preset key: unique
    uint8_t     steps;  // 0: continuous, else a list of this many
    float       initial;
};

// Each type's run, in order. Names say the type, so the keys are unique.
constexpr FxParam kFxParams[] = {
    // Chorus
    {"Chorus Dry/Wet", 0, 0.5f},
    {"Chorus Depth", 0, 0.4f},
    {"Chorus Speed", 0, 0.35f},
    {"Chorus Feedback", 0, 0.5f}, // bipolar
    {"Chorus Center", 0, 0.5f},
    {"Chorus Spread", 0, 0.6f},
    {"Chorus Shape", 2, 0.25f},   // Sine, Triangle
    {"Chorus Stages", 4, 0.375f}, // 2, 4, 6, 8
    {"Chorus High-Cut", 0, 0.1f},
    {"Chorus Low-Cut", 0, 0.1f},
    // Flanger
    {"Flanger Dry/Wet", 0, 0.5f},
    {"Flanger Depth", 0, 0.7f},
    {"Flanger Speed", 0, 0.3f},
    {"Flanger Feedback", 0, 0.75f}, // bipolar
    {"Flanger Center", 0, 0.35f},
    {"Flanger Phase Diff", 0, 0.25f},
    {"Flanger Shape", 2, 0.75f}, // Sine, Triangle
    // Phaser
    {"Phaser Dry/Wet", 0, 0.5f},
    {"Phaser Depth", 0, 0.6f},
    {"Phaser Speed", 0, 0.3f},
    {"Phaser Feedback", 0, 0.7f}, // bipolar
    {"Phaser Center", 0, 0.45f},
    {"Phaser Spread", 0, 0.4f},
    {"Phaser Phase Diff", 0, 0.25f},
    {"Phaser Shape", 2, 0.25f},   // Sine, Triangle
    {"Phaser Stages", 5, 0.3f},   // 2, 4, 8, 12, 16
    {"Phaser Model", 2, 0.25f},   // Nave, PPG
    // Tremolo
    {"Tremolo Depth", 0, 0.5f},
    {"Tremolo Speed", 0, 0.5f},
    {"Tremolo Phase Diff", 0, 0.0f},
    {"Tremolo Shape", 3, 0.15f}, // Sine, Triangle, Square
    {"Tremolo Smooth", 0, 0.3f},
    // Drive
    {"Drive Dry/Wet", 0, 1.0f},
    {"Drive Amount", 0, 0.4f},
    {"Drive Type", 5, 0.3f}, // PNP, Tube, PickUp, Diode, Crunch
    {"Drive Gain", 0, 0.5f}, // bipolar
    // Compressor
    {"Compr Ratio", 0, 0.35f},
    {"Compr Threshold", 0, 0.6f},
    {"Compr InGain", 0, 0.5f},  // bipolar
    {"Compr OutGain", 0, 0.5f}, // bipolar
    {"Compr Auto Gain", 2, 0.75f}, // Off, On
    {"Compr Attack", 0, 0.3f},
    {"Compr Release", 0, 0.4f},
    {"Compr Look Ahead", 0, 0.0f},
    // EQ
    {"EQ Dry/Wet", 0, 1.0f},
    {"EQ Gain", 0, 0.5f}, // bipolar
    {"EQ Low Freq", 0, 0.4f},
    {"EQ Low Gain", 0, 0.5f}, // bipolar
    {"EQ Mid Freq", 0, 0.4f},
    {"EQ Mid Gain", 0, 0.5f},
    {"EQ Mid Q", 0, 0.4f},
    {"EQ Mid 2 Freq", 0, 0.6f},
    {"EQ Mid 2 Gain", 0, 0.5f},
    {"EQ Mid 2 Q", 0, 0.4f},
    {"EQ High Freq", 0, 0.6f},
    {"EQ High Gain", 0, 0.5f},
    // Delay
    {"Delay Dry/Wet", 0, 0.3f},
    {"Delay Left", 0, 0.75f},
    {"Delay Right", 0, 0.8f},
    {"Delay Link", 2, 0.25f},  // Off, On
    {"Delay Sync", 2, 0.25f},  // Off, On
    {"Delay Ratio Left", 18, 9.5f / 18},  // 1/8
    {"Delay Ratio Right", 18, 10.5f / 18}, // 1/16.
    {"Delay Feedback", 0, 0.4f},
    {"Delay HighCut", 0, 0.3f},
    {"Delay LowCut", 0, 0.1f},
    // Reverb
    {"Reverb Dry/Wet", 0, 0.25f},
    {"Reverb Time", 0, 0.5f},
    {"Reverb Color", 0, 0.5f}, // bipolar
    {"Reverb Mod Depth", 0, 0.3f},
    {"Reverb Mod Rate", 0, 0.3f},
    {"Reverb Pre-Delay", 0, 0.15f},
};
constexpr int kNumFxParams = sizeof(kFxParams) / sizeof(kFxParams[0]);

// Where each type's run starts, and how long it is (Off has none).
constexpr int kFxCount[kNumFxTypes] = {0, 10, 7, 10, 5, 4, 8, 12, 10, 6};
constexpr int kFxFirst[kNumFxTypes] = {0, 0, 10, 17, 27, 32, 36, 44, 56, 66};
static_assert(kFxFirst[kNumFxTypes - 1] + kFxCount[kNumFxTypes - 1] == kNumFxParams,
              "the effects' runs don't add up");

// Synced delay lengths, in beats: 1/1 down to 1/64.
constexpr int   kNumDelayRatios              = 18;
constexpr float kDelayRatioBeats[kNumDelayRatios] = {
    4.0f, 3.0f, 8.0f / 3.0f, 2.0f, 1.5f, 4.0f / 3.0f, 1.0f, 0.75f, 2.0f / 3.0f,
    0.5f, 0.375f, 1.0f / 3.0f, 0.25f, 0.1875f, 1.0f / 6.0f, 0.125f, 1.0f / 12.0f, 0.0625f,
};

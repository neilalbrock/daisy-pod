// Wavebyte - the modulation matrix's sources and targets
//
// Eight slots, each a Source, a Target and an Amount, with the Protein's
// lists (its manual's appendix). Every voice works out its own slots once
// per block (Voice::Render): envelopes, LFOs, keytrack, velocity and the
// per-note MPE values differ from voice to voice; the MIDI controllers and
// the global LFOs are the same for all and live in Controllers.
//
// Sources are 0..1 (envelopes, controllers, velocity, Random) or -1..1
// (LFOs, pitch bend, keytrack, Random Bipol). An amount of 1 moves a target
// by its kRange, in the target's units.
//
// libDaisy-free.

#pragma once

#include <cstdint>

namespace mod
{
enum Source : uint8_t
{
    kOff,
    kWaveEnv,
    kFilterEnv,
    kAmpEnv,
    kLfo1,
    kLfo1Wheel, // LFO 1 times the mod wheel
    kLfo1Touch, // ... times aftertouch
    kLfo2,
    kLfo2Wheel,
    kLfo2Touch,
    kModWheel,
    kPitchBend,
    kNotePitch, // MPE per-note bend
    kChanPressure,
    kPolyPressure,
    kMpeY,
    kPedal,
    kExpression,
    kBreath,
    kCc22,
    kCc31 = kCc22 + 9,
    kKeytrack,
    kVelocity,
    kReleaseVelocity,
    kRandom,
    kRandomBi,
    kConstant,
    kNumSources,
};

enum Target : uint8_t
{
    kNone,
    kWave1,
    kWave2,
    kPitch, // both oscillators
    kPitch1,
    kPitch2,
    kLevel1,
    kLevel2,
    kDirt,
    kCutoff,
    kResonance,
    kVolume,
    kPan,
    kLfo1Speed,
    kLfo2Speed,
    kEnvStages, // then attack, decay, sustain, release of the wave, filter and amp envelopes
    kNumTargets = kEnvStages + 12,
};

// What an amount of 1 does to each target: waves, semitones, level,
// octaves of cutoff, resonance, a factor of volume, pan, octaves of LFO
// speed, and octaves of envelope rate (faster for positive) or sustain.
constexpr float kRange[kNumTargets] = {
    0.0f,  64.0f, 64.0f, 24.0f, 24.0f, 24.0f, 1.0f, 1.0f, 1.0f, 10.0f, 1.0f, 1.0f, 2.0f, 5.0f, 5.0f,
    6.0f,  6.0f,  1.0f,  6.0f, // wave envelope
    6.0f,  6.0f,  1.0f,  6.0f, // filter envelope
    6.0f,  6.0f,  1.0f,  6.0f, // amp envelope
};

constexpr int kSlots = 8;

struct Slot
{
    Source source;
    Target target;
    float  amount; // in the target's units
};

// What all voices read: the MIDI controllers (0..1, bend -1..1) and the
// global LFOs. With MPE, channel 1 is the master channel and every other
// channel carries one note's bend (-1..1, of 48 semitones), Y (CC 74) and
// pressure.
struct Controllers
{
    float wheel      = 0.0f;
    float bend       = 0.0f;
    float pressure   = 0.0f; // channel pressure
    float pedal      = 0.0f;
    float expression = 1.0f; // full until a pedal says otherwise
    float breath     = 0.0f;
    float cc[10]     = {};   // CC 22-31
    float lfo[2]     = {};   // the global LFOs this block
    bool  mpe        = false;

    float channel_bend[16]     = {};
    float channel_y[16]        = {};
    float channel_pressure[16] = {};
    float poly_pressure[128]   = {}; // by note, without MPE
};
} // namespace mod

# Wavebyte

An 8-voice wavetable synth, our own take on the Waldorf Protein. Each voice
has two wavetable oscillators reading 8-bit waves with no interpolation,
as the Microwave's chip did. A filter follows, after the Protein's model of
the CEM chip, or a Drive in its place, then three envelopes. Play it over
MIDI, or with button 1.

```sh
make APP=wavebyte program-dfu
```

Every control is also on the [web page](#the-web-page). Eight
[presets](#presets) are kept in flash.

This is the first of several stages (see [Roadmap](#roadmap)). It has one
layer, the oscillators, filter, envelopes, voice modes and glide. Next come
Dirt's other noises and Flavour, then the LFOs, mod matrix and MPE, the
effects, the arpeggiator and sequencer, and the Protein's four layers.

## Signal flow

```
per voice (8):
  osc 1 (table, wave) --+
  osc 2 (table, wave) --+-- + Dirt -> filter or Drive -> amp (pan) --+
                                                                     |
  wave env -> both waves   filter env -> cutoff   amp env -> level   |
                                                                     |
all voices --------------------------------------------------------- + -> Master -> limiter -> out
```

## Wavetables

64 tables of 64 waves each, in the Microwave's layout. Waves 0-60 are the
table's own, a sweep from one sound to another. Waves 61, 62 and 63 are
the same in every table: triangle, square and saw, as on the Protein.
**Limit Waves** keeps an oscillator off those three, so a sweep stays
inside the table.

The Protein's tables are the Microwave 1's, and those belong to Waldorf. So
these are our own, generated at power-up: families of sweeps named after
what they do. They include resonant peaks, harmonics added one by one,
pulse width, hard sync, vowel formants, clipping and folding, two-operator
FM, bells and mallets, organ drawbars, combs, echoes within the cycle,
phase distortion, fewer steps or bits, random spectra, morphs between basic
shapes, and byte patterns.

An oscillator steps from wave to wave rather than crossfading, and plays
each wave's 128 samples with no interpolation. That stepping and the 8-bit
samples are the grit: the aliasing is part of the sound.

## Controls

The two knobs are shared across seven pages. Press the encoder to change
page (LED 2 shows which). As on the Protein's panel, each knob has a second
job under **Shift** (button 2): hold Shift while turning, or tap it to keep
it on (LED 1 glows blue) and tap again to let go. After a change of page or
Shift a knob does nothing until you move it, so values don't jump. Turning
the encoder sets that page's choice, and LED 2 flashes it for a second.

| Page (LED 2) | Knob 1 / Shift | Knob 2 / Shift | Encoder turn |
|---|---|---|---|
| 1 Osc 1 (red) | Wave / Level | Env Amnt / Vel | Table |
| 2 Osc 2 (yellow) | Wave / Level | Env Amnt / Vel | Table |
| 3 Pitch (green) | Semitone 2 / Semitone 1 | Detune 2 / Detune 1 | Voice: Poly, Mono, Legato |
| 4 Filter (teal) | Cutoff / Keytrack | Resonance / Dirt | Type: LP 24, LP 12, HP, Drive, Off |
| 5 Env & Glide (blue) | Filter Env / Filter Vel | Glide / Bend Range | Glide: Onset, Legato |
| 6 Envelopes (violet) | Attack / Sustain | Decay / Release | Envelope: Wave, Filter, Amp |
| 7 Amp (magenta) | Volume / Vel Amnt | Pan / Master | Pan: Fix, Rand 1-5 |

| Control | Function |
|---|---|
| Encoder press | Next page |
| Encoder long press (0.6 s) | All notes off (LED 1 flashes red). On the preset page, it saves instead |
| Page 8 (LED 2: the slot) | Presets: the encoder turn selects a slot (see [Presets](#presets)) |
| Button 1 | Play: sounds the play note while held, as the Protein's Play button does. Turn the encoder while holding it to change the note, by octaves with Shift |
| Button 2 | Shift |

LED 1 shows the last voice's amp envelope, and glows blue with Shift on.

### What they do

- **Wave:** where in the table the oscillator starts, 0-63. **Env Amnt**
  moves it with the wave envelope, up to the whole table either way.
  **Vel** adds more at full velocity.
- **Level:** the oscillator's volume.
- **Semitone:** ±24. **Detune:** ±1 semitone (100 cents). Fixed pitch (on
  the web page) stops an oscillator following the notes.
- **Voice:** Poly gives a voice to each note. With eight notes held, the
  next takes a free voice, else the oldest released one, else the oldest.
  Mono plays one note at a time, starting the envelopes on every note. Let
  go of the note playing and the highest key still held takes over, as on
  the Protein. Legato is the same, but only a note played from silence
  starts the envelopes.
- **Glide:** off at 0, then 2 ms to 5 s. Onset glides every note from the
  last one played. Legato glides only while another key is held.
- **Bend Range:** 0-12 semitones for pitch bend.
- **Cutoff:** 20 Hz to 20 kHz. **Keytrack:** -200% to +200% around C4 (note
  60). At +100% the cutoff follows the keys exactly.
- **Resonance:** past about 0.8, LP 24 sings on its own.
- **Filter Env:** up to 8 octaves either way. **Filter Vel** adds more at
  full velocity.
- **Drive:** in the filter's place. Cutoff, and everything that moves it,
  sets the amount; Resonance picks the curve, split evenly between five:
  - PNP: a transistor's uneven clipping
  - Tube: biased, so asymmetric
  - PickUp: folds back as the level rises, so it changes with the envelope
  - Diode: hard
  - Crunch: a sine folder, FM-like at high amounts
- **Dirt:** white noise into the filter. More kinds come in the next stage.
- **Envelopes:** attack 0.5 ms to 10 s, in a straight line. Decay and
  release are exponential, 1 ms to 20 s. A new note attacks from wherever
  the envelope is, so a retriggered voice doesn't click.
- **Volume / Vel Amnt:** the sound's level, and how much velocity sets it.
  **Master** is the overall level, kept with the settings rather than a
  preset, like the Protein's Volume dial.
- **Pan:** Fix holds every voice at Pan. Rand 1-5 throws each note further
  from it at random.

Starting values: Reso Saw on both oscillators, osc 2 a few cents up,
LP 24 at 7 kHz with a little resonance and filter envelope, Poly, no glide.

## MIDI

Wavebyte listens on every channel, on the Pod's TRS MIDI input and over USB:

| Message | |
|---|---|
| Notes | Note on and off, with velocity |
| Pitch bend | Over Bend Range |
| CC 64 | Sustain pedal: holds notes released while it's down |
| CC 120, 123 | All notes off |
| Program change 1-8 (sent as 0-7) | Preset slot 1-8 |

## Presets

Eight preset slots, kept in the Seed's flash, so they survive power-off and
reflashing. Every app keeps its own.

- **On the Pod:** page 8 is the preset page. Turning the encoder there
  selects a slot and loads its preset; an empty slot is only selected,
  keeping the sound, ready to save into. A long press on the encoder saves
  the current settings to the slot. The knobs do nothing on this page.
  LED 2 shows the slot: a colour each, dim if empty, blinking off while the
  sound differs from what's saved, and flickering white on a save. Before
  any slot is selected it's dim white.
- **Over MIDI:** program change 1-8 (sent as 0-7) selects slot 1-8.
- **On the web page:** a button per slot, and Save. Selecting the slot
  already selected reloads it, dropping any changes.

A preset holds the sound: every knob but Master, the tables and the other
choices, Limit Waves and Fixed. Master and the play note are saved on their
own, a few seconds after they last change. At power-up they come back, then
the preset last selected.

**Factory reset:** hold both buttons while powering up the Pod (through
the bootloader's 2.5 s wait) until both LEDs flash white, then let go. Or
press Factory reset on the web page. Every preset and saved setting
for this app is erased (the other apps keep theirs) and everything goes
back to its defaults.

## The web page

Run `make web` and open http://localhost:8000 in Chrome or Edge with the Pod
connected by USB. The page shows the output level, CPU, and traces of the
last voice played: both wave positions, its cutoff, its amp envelope, and
how many voices are sounding. Every control is there too, with Limit Waves,
Fixed, the play note and a Play switch, which the Pod doesn't have. See
[web/README.md](../../web/README.md).

## Roadmap

From the Protein's manual (firmware 1.02). Each stage is proven on the Pod
before the next.

1. **Done:** one layer of 8 voices, 64 generated tables, oscillators,
   filter types and Drive, three ADSRs with velocity, Poly, Mono and Legato,
   Glide, pitch bend, Play and Shift, presets and the web page.
2. **Character:** Dirt's other noises (Crackle, Geiger, Click, Burst 1-3,
   and Ring Mod of osc 1 by osc 2); Flavour, small deliberate variations,
   mostly in wave position and pitch (its osc 1 pitch drift scales with Bend
   Range, as on the Protein, so a range of 0 means none); a model of the
   per-voice DACs; and a Clean mode for the oscillators beside the 8-bit
   one.
3. **Modulation:** two LFOs (seven shapes, Speed or a tempo Ratio from 4
   bars to 1/64, Free or Clocked, Global or per Voice); the 8-slot matrix
   with the Protein's sources and targets; aftertouch; MPE (per-note bend
   over ±48 semitones, CC 74 as MPE Y, pressure as poly aftertouch); and a
   fixed CC map, since the Pod can't do CC learn comfortably.
4. **Effects:** two global slots, each type used once. The types are
   Chorus, Flanger, Phaser, Tremolo, Drive, Compressor, EQ, Delay and
   Reverb (on `Plate`, in RAM_D2). Each layer goes dry, through FX 1 then
   FX 2, or through FX 2 only.
5. **Play options:**
   - Chord and Scale.
   - The arpeggiator: seven directions, octaves, swing, gate, Reset, 16
     rhythms.
   - The 32-step sequencer: pitch offsets and velocity per step, five
     directions, Reset, scale snapping, step recording, 16 patterns.
6. **Layers:** four, sharing the 8 voices. In Single mode one plays; Multi
   offers Layered, Round Robin, Random Robin and MIDI Split. About 450
   parameters in all, which the web link's 14-bit indices and
   `PresetsFor` already allow.
7. **Maybe:** CC learn, and importing wavetables from the web page.

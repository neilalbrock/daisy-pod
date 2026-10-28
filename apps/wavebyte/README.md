# Wavebyte

An 8-voice wavetable synth, our own take on the Waldorf Protein. Each voice
has two wavetable oscillators reading 8-bit waves with no interpolation,
as the Microwave's chip did (or smoothly, in Clean mode), and Dirt's
noises. A filter follows, after the Protein's model of the CEM chip, or a
Drive in its place, then an amplifier that models the voice's DAC, and
three envelopes. Two LFOs and an 8-slot mod matrix move it, Flavour adds
small, steady variations, and two effect slots finish it. As on the
Protein, four layers, each a sound of its own, share the eight voices:
play one, or several together, in turn or by MIDI channel. Play it over
MIDI (MPE too), or with button 1.

```sh
make APP=wavebyte program-dfu
```

Every control is also on the [web page](#the-web-page). Eight
[presets](#presets) are kept in flash.

This is the sixth of several stages (see [Roadmap](#roadmap)). It has the
oscillators, Dirt, filter, envelopes, voice modes, glide, Flavour, the DAC
model, the LFOs, the mod matrix, aftertouch, MPE, the effects and the
Protein's four [layers](#layers). The arpeggiator and sequencer are
parked: a DAW or sequencer does their job.

## Signal flow

```
notes -> layers A-D (Single, Layered, Round Robin, Random Robin, MIDI Split)

per voice (8, shared by the layers; each plays its layer's sound):
  osc 1 (table, wave) --+
  osc 2 (table, wave) --+-- + Dirt -> filter or Drive -> amp, DAC (pan) --+
                                                                          |
  wave env -> both waves   filter env -> cutoff   amp env -> level        |
  Flavour -> waves, osc 1 pitch, cutoff, level                            |
  mod matrix (8 slots): envelopes, LFOs, MIDI -> 26 targets               |
                                                                          |
by the voice's layer's routing:                                           |
  FX 1 > FX 2: ----------------------------> FX 1 --+                     |
  FX 2 only:   -------------------------------------+-> FX 2 --+          |
  Dry:         --------------------------------------------------+ -> Master -> limiter -> out
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

### The AKWF bank

A second bank of 64 tables is made from
[AKWF-FREE](https://github.com/KristofferKarlAxelEkstrand/AKWF-FREE), the
Adventure Kid Waveforms by Kristoffer Ekstrand: a library of single-cycle
waveforms released into the public domain (CC0). See
[Credits](#credits). Pick one with **Shift** and the encoder turn on an Osc
page (LED 2 flashes it); **Off**, the first, goes back to the Table. Each
oscillator, in each layer, has its own.

AKWF's waves are single cycles: an instrument's timbre at one moment, not
a sweep. So each table is built at power-up, like ours, by one of two
recipes:

- **Key waves crossfaded** across the table, as the Microwave filled its
  tables between key waves: taken along a path through a folder (each
  wave the closest left to the one before, from the darkest), or picked by
  hand. Voices and instruments (Voice, Orchestra, Cello, Violin, Sax,
  Reeds, Flute, Piano, EPiano, E Organ, Clavinet, guitars, basses,
  Theremin, Birds, Stringbox, Bass Brass), then synth shapes (FM, Chip,
  Video Game, Osc Chip, Bitreduce, Distorted, Granular, Overtone, Linear,
  Hand Drawn, the band-limited shapes and four from the miscellaneous
  folders). Voice Step, EPiano, FM Steps and Chip step through 61 waves
  rather than crossfading.
- **One sampled wave through a family:** hard sync (Sync Cello, Sax,
  Voice, Organ, Bass), folding (Fold ...), the wave minus a shifted copy
  of itself (PWM ...), or squeezed into a narrowing window, like a formant
  (Sqz ...). Unlike ours, these are band-limited, so they're clean until
  the 8-bit oscillator plays them.

Waves 61-63 are triangle, square and saw here too. The bank was chosen by
the numbers rather than by ear: `tools/akwf_tables.py` makes it (see
[docs/akwf.md](docs/akwf.md)), and renders a sweep through each table to
listen to.

In 8-bit mode an oscillator steps from wave to wave rather than
crossfading, and plays each wave's 128 samples with no interpolation. That
stepping and the 8-bit samples are the grit: the aliasing is part of the
sound. **Clean** mode (on the Character page) interpolates between samples
and crossfades between neighbouring waves, so a sweep through a table is
smooth and the top end is softer. It costs a little more CPU.

## Controls

The two knobs are shared across eleven pages. Press the encoder to change
page (LED 2 shows which). As on the Protein's panel, each knob has a second
job under **Shift** (button 2): hold Shift while turning, or tap it to keep
it on (LED 1 glows blue) and tap again to let go. After a change of page,
Shift or layer a knob does nothing until you move it, so values don't
jump. Turning the encoder sets that page's choice, and LED 2 flashes it
for a second. With Shift, turning it picks an AKWF table instead on the
Osc pages, the Dirt type on the Filter page, and the routing on the FX
page.

The pages edit the layer picked on the Layer page (page 12). Master,
Flavour, DAC and the effects are shared by the layers, so they're the same
whichever layer is picked.

| Page (LED 2) | Knob 1 / Shift | Knob 2 / Shift | Encoder turn |
|---|---|---|---|
| 1 Osc 1 (red) | Wave / Level | Env Amnt / Vel | Table. With Shift, an [AKWF](#the-akwf-bank) table, or Off |
| 2 Osc 2 (orange) | Wave / Level | Env Amnt / Vel | Table. With Shift, an [AKWF](#the-akwf-bank) table, or Off |
| 3 Pitch (yellow) | Semitone 2 / Semitone 1 | Detune 2 / Detune 1 | Voice: Poly, Mono, Legato |
| 4 Filter (lime) | Cutoff / Keytrack | Resonance / Dirt | Type: LP 24, LP 12, HP, Drive, Off. With Shift, Dirt type: Static, Crackle, Geiger, Click, Burst 1-3, Ring Mod |
| 5 Env & Glide (green) | Filter Env / Filter Vel | Glide / Bend Range | Glide: Onset, Legato |
| 6 Envelopes (aqua) | Attack / Sustain | Decay / Release | Envelope: Wave, Filter, Amp |
| 7 Amp (azure) | Volume / Vel Amnt | Pan / Master | Pan: Fix, Rand 1-5 |
| 8 Character (blue) | Flavour (Shift: the same) | DAC (Shift: the same) | Osc: 8-bit, Clean |
| 9 LFOs (violet) | Speed / Ratio | Shape / Mode | LFO: 1, 2 |
| 10 Mod Matrix (purple) | Source / Target | Amount (Shift: the same) | Slot: 1-8 |
| 11 FX (pink) | Type / Param | Amount / Value | Slot: FX 1, FX 2. With Shift, the layer's routing: Dry, FX 1 > FX 2, FX 2 only |

| Control | Function |
|---|---|
| Encoder press | Next page |
| Encoder long press (0.6 s) | All notes off (LED 1 flashes red). On the Layer page, it switches the layer on or off instead, and on the preset page, it saves |
| Page 12 (LED 2: the layer's colour) | Layers: the encoder turn picks the layer the pages edit, A-D, and with Shift, the mode (see [Layers](#layers)). The knobs do nothing here |
| Page 13 (LED 2: the slot) | Presets: the encoder turn selects a slot (see [Presets](#presets)) |
| Button 1 | Play: sounds the play note while held, as the Protein's Play button does. Turn the encoder while holding it to change the note, by octaves with Shift |
| Button 2 | Shift |

LED 1 glows in the colour of the layer being edited (A red, B yellow, C
green, D magenta), lit up by the amp envelope of that layer's last voice,
and blue with Shift on.

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
- **Resonance:** past about 0.8, LP 24, LP 12 and HP sing on their own, at
  the cutoff's pitch, even with both oscillators down. Just past 0.8 the
  tone takes a moment to build (longest at low cutoffs); at full resonance
  it's almost instant. On LP 24 a note playing gets quieter as the tone
  takes over, as on a ladder filter.
- **Filter Env:** up to 8 octaves either way. **Filter Vel** adds more at
  full velocity.
- **Drive:** in the filter's place. Cutoff, and everything that moves it,
  sets the amount; Resonance picks the curve, split evenly between five:
  - PNP: a transistor's uneven clipping
  - Tube: biased, so asymmetric
  - PickUp: folds back as the level rises, so it changes with the envelope
  - Diode: hard
  - Crunch: a sine folder, FM-like at high amounts
- **Dirt:** noise into the filter, beside the oscillators, at the Dirt
  level. The type is set with Shift and the encoder on the Filter page:
  - Static: white noise
  - Crackle: short random pulses, many faint and a few loud, like a record
  - Geiger: sparse random ticks, like a Geiger counter
  - Click: one short noise pulse as each note starts
  - Burst 1, 2, 3: one noise burst as each note starts, 10, 40 and 150 ms
  - Ring Mod: osc 1 times osc 2, at their full level whatever the Level
    knobs say. Turn both Levels down to hear only the ring modulation
- **Envelopes:** attack 0.5 ms to 10 s, in a straight line. Decay and
  release are exponential, 1 ms to 20 s. A new note attacks from wherever
  the envelope is, so a retriggered voice doesn't click.
- **Volume / Vel Amnt:** the sound's level, and how much velocity sets it.
  **Master** is the overall level, kept with the settings rather than a
  preset, like the Protein's Volume dial.
- **Pan:** Fix holds every voice at Pan. Rand 1-5 throws each note further
  from it at random.
- **Flavour** (shared by the layers): small, steady variations, mostly in
  wave position and pitch, as on the Protein. Each voice drifts at rates
  of its own, so the variations are methodical rather than random, and
  each note starts a little differently. At full: both wave positions move
  about ±3 waves, the cutoff ±0.3 octaves, the level a little per note,
  and osc 1's pitch drifts by up to 8% of the layer's Bend Range (±16
  cents at a range of 2). As on the Protein, a Bend Range of 0 means no
  pitch drift at all.
- **DAC** (shared by the layers): the amplifier models each voice's
  digital-to-analog converter. The level is applied before conversion, at
  12 bits just above 0 down to 6 bits at full, so quiet tails and soft
  notes turn grainy. Each voice's converter also has its own small gain
  error and a little crossover distortion around zero. At 0 the converter
  is ideal.
- **Osc:** 8-bit, or Clean (see [Wavetables](#wavetables)).
- **LFOs:** two, chosen with the LFO page's encoder turn.
  - **Speed:** 0.02 to 41 Hz, when Free.
  - **Ratio:** the cycle as a share of the tempo, when Clocked: 4/1 (four
    bars) to 1/64, with dotted (.) and triplet (T) values between.
  - **Shape:** Sine, Triangle, Pulse, Saw (falling), Saw Up, Random (glides
    to a new random value each cycle) and S & H (jumps to one).
  - **Mode:** Free Voice gives every note its own LFO, starting at the top
    of its cycle as the note starts. Free Global runs one LFO for every
    voice. Clocked runs one too, in time with the tempo, as on the
    Protein, where Clocked always syncs the voices. The tempo is MIDI
    clock while it's running (MIDI Start puts Clocked LFOs back to the
    start of their cycle), else the web page's Tempo.
- **Mod Matrix:** eight slots, chosen with the Mod Matrix page's encoder
  turn, each a Source, a Target and an Amount, as on the Protein. Source
  and Target step through their lists as the knob turns; Amount is
  bipolar, centred at no modulation, and squared (keeping its sign) so
  small amounts, such as vibrato, are easy to set. Knob 2 is Amount with
  Shift too, so it's always under your hand. Sources:
  - Wave Env, Filter Env, Amp Env
  - LFO 1, LFO 2, and each times the mod wheel (MW) or aftertouch (AT: the
    larger of channel and poly pressure)
  - Mod Wheel (CC 1), Pitch Bend, Note PB (MPE per-note bend), Ch Press,
    Poly AT (with MPE, the note's channel pressure), MPE Y (CC 74, MPE
    only), Pedal (CC 64), Expression (CC 11), Breath (CC 2), CC 22-31
  - Keytrack (around C4), Velocity, Rel Velo (release velocity), Random
    and Random Bipol (drawn per note), Constant

  Targets, with what full Amount does:
  - Wave 1, Wave 2: ±64 waves
  - Pitch (both oscillators), O1 Pitch, O2 Pitch: ±24 semitones
  - O1 Vol, O2 Vol, Dirt, Resonance: the whole range
  - Cutoff: ±10 octaves. Volume: silent to double. Pan: side to side
  - LFO 1 Speed, LFO 2 Speed: ±5 octaves (not Clocked). A Free Global
    LFO follows the matrix of the voice last played
  - The three envelopes' Att, Dec and Rel rates: ±6 octaves, positive
    faster; and Sus: the whole range. Each voice works its own out, so
    Velocity to Amp Env Att works per note

  Envelope sources are a block (0.33 ms) behind, so the envelopes can
  modulate their own rates.

### Layers

Four layers, A-D, as on the Protein. Each is a whole sound: everything
above but Flavour and DAC, which are shared, as are Master and the effects.
They share the eight voices: a note takes a free voice, else the oldest
released, else the oldest, whichever layer has it, so two notes played on
four layers at once take every voice.

The Layer page (page 12) picks the layer the other pages edit with the
encoder turn. LED 2 shows its colour there, dim if it doesn't play. With
Shift, the encoder turn sets the mode:

- **Single:** the layer picked plays (on its own).
- **Layered:** every active layer plays each note.
- **Round Robin:** each note goes to the next active layer up, wrapping
  round from D to A.
- **Random Robin:** each note goes to an active layer at random.
- **MIDI Split:** each active layer plays the notes on its own channel:
  A on the split channel (1 unless the web page sets another), B on the
  next and so on, as the Protein's Receive Channel sets.

A long press on the encoder on the Layer page switches the layer on or off
(Active, in the Multi modes). Layer A starts active, B-D off, all with the
same sound. A note is let go on the layers it went to, whatever the mode
or layer picked has become since. A layer's voice mode is its own: a
Mono layer plays one voice of the eight, and a Poly layer beside it the
rest.

The web page's **Layer Actions** card, as the Protein's Layer Action menu,
works on the layer being edited (the tab showing):

- **Init Layer:** back to the starting sound.
- **Copy Layer**, then **Paste Layer** onto another: the whole sound, kept
  until power-off.
- **Load Layer:** one layer of a stored preset, chosen with **Load Slot**
  and **Load From**, into this one. The preset selected stays selected,
  showing as changed.

None of them touches Active, which belongs to the Multi modes rather than
the sound.

Each layer has its own LFOs, so a Free Global LFO is one per layer, not
one for the whole synth. The fixed CC map sets the layer being edited, or
in MIDI Split, the layer on the CC's channel. MPE doesn't work in MIDI
Split, which uses the channels itself.

### Effects

Two slots after the voices, as on the Protein. The FX page's encoder turn
picks the slot; with Shift it sets the routing of the layer being edited:
**Dry** (no effects), **FX 1 > FX 2**, or **FX 2 only**. Each layer has
its own routing, and a slot runs while a layer that plays (or still
sounds) is routed through it. Knob 1 picks the slot's effect, Off or
one of nine, and knob 2 sets its **Amount**, the first value in its list
below (Dry/Wet for most). With Shift, knob 1 picks a parameter from the
list and knob 2 sets its **Value**. LED 2 flashes what knob 1 picks: the
effect, or the parameter's place along its list. The web page shows every
effect's values in a card of its own.

Each effect can be in one slot at a time, so its values belong to the
effect, not the slot: move Reverb from FX 2 to FX 1 and its settings go
with it. If FX 2 is set to the same effect as FX 1 it stays dry (the
Protein doesn't offer it). A change of effect or routing fades the slot
out and the new effect in, starting afresh (no old echoes).

| Effect | Values, in knob 1's order with Shift |
|---|---|
| Chorus | Dry/Wet, Depth (to 6 ms), Speed (0.05-13 Hz), Feedback (±), Center (the right side's LFOs offset from the left's, to 180°), Spread (how far apart its voices' delays are: low rings more), Shape (Sine, Triangle), Stages (2, 4, 6, 8 delays, half each side), High-Cut, Low-Cut |
| Flanger | Dry/Wet, Depth, Speed (0.02-10 Hz), Feedback (±), Center (the longest delay, 0.3-10 ms), Phase Diff (to 180°), Shape |
| Phaser | Dry/Wet, Depth (±3 octaves of sweep), Speed, Feedback (±), Center (100 Hz-8 kHz), Spread, Phase Diff, Shape, Stages (2, 4, 8, 12, 16 allpasses, half each side), Model (Nave: smooth, stages spread; PPG: a stepped sweep, stages together, feedback inverted; our own readings of the two) |
| Tremolo | Depth, Speed (0.1-13 Hz), Phase Diff, Shape (Sine, Triangle, Square), Smooth (a slew on the LFO, to 40 ms) |
| Drive | Dry/Wet, Amount, Type (the filter's curves: PNP, Tube, PickUp, Diode, Crunch), Gain (±12 dB) |
| Compressor | Ratio (1:1-20:1; the Amount knob here), Threshold (-40 to 0 dB), InGain, OutGain (±12 dB), Auto Gain (makes up half what's taken at full level), Attack (0.1-100 ms), Release (10 ms-1 s), Look Ahead (to 10 ms) |
| EQ | Dry/Wet, Gain (±12 dB), Low Freq (20 Hz-1 kHz shelf), Low Gain, Mid Freq (100 Hz-8 kHz), Mid Gain, Mid Q, Mid 2 Freq (200 Hz-12 kHz), Mid 2 Gain, Mid 2 Q, High Freq (1-16 kHz shelf), High Gain (gains ±18 dB) |
| Delay | Dry/Wet, Left, Right (1 ms-2 s), Link (Right follows Left), Sync (to the tempo, by Ratio), Ratio Left, Ratio Right (1/1 to 1/64, dotted and triplet too), Feedback, HighCut, LowCut (in the feedback, so each echo is darker or thinner). Times glide when changed, as on tape |
| Reverb | Dry/Wet, Time (0.3-19 s), Color (below the middle darker, above thinner), Mod Depth, Mod Rate (0.1-6 Hz), Pre-Delay (to 60 ms) |

The tempo for Delay Sync is the same as for Clocked LFOs: MIDI clock while
it runs, else the web page's Tempo.

Starting values: Reso Saw on both oscillators, osc 2 a few cents up,
LP 24 at 7 kHz with a little resonance and filter envelope, Poly, no glide,
no Flavour, DAC at 0.3, 8-bit. LFO 1 is a 5 Hz sine (Free Voice), LFO 2 a
0.5 Hz triangle (Free Global), and slot 1 is LFO 1 * MW to Pitch at +1.3%
(±0.3 semitones): vibrato on the mod wheel. FX 1 is off and FX 2 is
Reverb at 25% wet; every effect starts from values that suit it.

## MIDI

Wavebyte listens on every channel, on the Pod's TRS MIDI input and over USB:

| Message | |
|---|---|
| Notes | Note on and off, with velocity and release velocity |
| Pitch bend | Over Bend Range |
| Channel and poly pressure | Ch Press and Poly AT in the mod matrix |
| CC 1, 2, 11, 22-31 | Mod Wheel, Breath, Expression and CC 22-31 in the mod matrix |
| CC 64 | Sustain pedal: holds notes released while it's down. Also Pedal in the matrix |
| CC 120, 123 | All notes off |
| Program change 1-8 (sent as 0-7) | Preset slot 1-8 |
| Clock, Start, Continue, Stop | The tempo for Clocked LFOs |

The CC map sets the layer being edited, or in MIDI Split, the layer on the
CC's channel. Notes go to the layers by the [layer mode](#layers).

**MPE:** switch it on with MPE on the web page (it's kept with the
settings, not a preset; it doesn't work in MIDI Split). Channel 1 is the master channel: its pitch bend,
pressure and CCs act on every note. Every other channel carries one note,
and its pitch bend bends that note over ±48 semitones (none with Bend
Range at 0, as on the Protein), its CC 74 is MPE Y and its pressure is
Poly AT, for the mod matrix. A note is let go on the channel it came on.

**CC map:** fixed, since the Pod can't learn CCs comfortably as the
Protein does. Values are 0-127, with 64 exactly at the centre of bipolar
controls.

| CC | Parameter | CC | Parameter |
|---|---|---|---|
| 3, 9 | Table 1, 2 (0-63) | 76 | Dirt |
| 12, 13 | AKWF 1, 2 (0 off, 1-64) | | |
| 5 | Glide | 77, 78 | Filter Env, Filter Vel |
| 7 | Volume | 79 | Bend Range |
| 10 | Pan | 85 | Amp Vel |
| 14-21 | Mod slot 1-8 Amount | 86, 87 | Flavour, DAC |
| 52-55 | Osc 1 Wave, Env Amnt, Level, Vel | 102-105 | Wave Env A, D, S, R |
| 56-59 | Osc 2 Wave, Env Amnt, Level, Vel | 106-109 | Filter Env A, D, S, R |
| 60, 61 | Semitone 1, Detune 1 | 110-113 | Amp Env A, D, S, R |
| 62, 63 | Semitone 2, Detune 2 | 114, 115 | LFO 1 Speed, Shape |
| 70 | Keytrack | 116, 117 | LFO 2 Speed, Shape |
| 71 | Resonance | 118 | Filter type (split evenly) |
| 74 | Cutoff (not with MPE) | 119 | Dirt type (split evenly) |

## Presets

Eight preset slots, kept in the Seed's flash, so they survive power-off and
reflashing. Every app keeps its own.

- **On the Pod:** page 13 is the preset page. Turning the encoder there
  selects a slot and loads its preset; an empty slot is only selected,
  keeping the sound, ready to save into. A long press on the encoder saves
  the current settings to the slot. The knobs do nothing on this page.
  LED 2 shows the slot: a colour each, dim if empty, blinking off while the
  sound differs from what's saved, and flickering white on a save. Before
  any slot is selected it's dim white.
- **Over MIDI:** program change 1-8 (sent as 0-7) selects slot 1-8.
- **On the web page:** a button per slot, and Save. Selecting the slot
  already selected reloads it, dropping any changes.

A preset holds the sound: all four layers (every knob but Master, the LFOs
and mod matrix among them, the tables, AKWF tables and the other choices,
Dirt type, Osc mode and FX routing among them, Limit Waves, Fixed and
Active), Flavour, DAC, both FX types, every effect's values, the layer
mode and the layer picked. Master, the play note, MPE,
Tempo and the split channel are saved on their own, a few seconds after
they last change. At power-up they come back, then the preset last
selected.

Layer A's settings are stored under the names they had before wavebyte
had layers, so a preset saved then loads as layer A, leaving B-D and the
mode as they were.

**Factory reset:** hold both buttons while powering up the Pod (through
the bootloader's 2.5 s wait) until both LEDs flash white, then let go. Or
press Factory reset on the web page. Every preset and saved setting
for this app is erased (the other apps keep theirs) and everything goes
back to its defaults.

## The web page

Run `make web` and open http://localhost:8000 in Chrome or Edge with the Pod
connected by USB. The page shows the output level, CPU, and traces of the
last voice played on the layer being edited: both wave positions, its
cutoff, its amp envelope, Flavour's pitch drift on osc 1, both LFOs as it
sees them, how many voices are sounding, and how much stack the firmware
has never used (Stack free: its margin in DTCM). Every control is there
too, with Limit Waves, Fixed, the play note, a Play switch, MPE, Tempo,
and the split channel, which the Pod doesn't have. Each
layer's cards are in a tab of their own, A-D, which follows the Pod's
Layer page both ways. See [web/README.md](../../web/README.md).

## Roadmap

From the Protein's manual (firmware 1.02). Each stage is proven on the Pod
before the next.

1. **Done:** one layer of 8 voices, 64 generated tables, oscillators,
   filter types and Drive, three ADSRs with velocity, Poly, Mono and Legato,
   Glide, pitch bend, Play and Shift, presets and the web page.
2. **Done:** Character: Dirt's other noises (Crackle, Geiger, Click, Burst
   1-3, and Ring Mod of osc 1 by osc 2); Flavour, small deliberate
   variations, mostly in wave position and pitch (its osc 1 pitch drift
   scales with Bend Range, as on the Protein, so a range of 0 means none);
   a model of the per-voice DACs; and a Clean mode for the oscillators
   beside the 8-bit one.
3. **Done:** Modulation: two LFOs (seven shapes, Speed or a tempo Ratio from 4
   bars to 1/64, Free or Clocked, Global or per Voice); the 8-slot matrix
   with the Protein's sources and targets; aftertouch; MPE (per-note bend
   over ±48 semitones, CC 74 as MPE Y, pressure as poly aftertouch); and a
   fixed CC map, since the Pod can't do CC learn comfortably.
4. **Done:** Effects: two global slots, each type used once. The types are
   Chorus, Flanger, Phaser, Tremolo, Drive, Compressor, EQ, Delay and
   Reverb (on `Plate`, in RAM_D2). Each layer goes dry, through FX 1 then
   FX 2, or through FX 2 only.
5. **Parked:** Play options. A DAW or sequencer does their job, so they
   wait unless that changes:
   - Chord and Scale.
   - The arpeggiator: seven directions, octaves, swing, gate, Reset, 16
     rhythms.
   - The 32-step sequencer: pitch offsets and velocity per step, five
     directions, Reset, scale snapping, step recording, 16 patterns.
6. **Done:** Layers: four, sharing the 8 voices. In Single mode one plays;
   Multi offers Layered, Round Robin, Random Robin and MIDI Split. Flavour,
   DAC and the effects are shared; each layer has its own routing. 413
   parameters in all. The presets' arrays and the web page's state moved
   to SDRAM to make room in DTCM. With all four layers running and the
   settings pushed, CPU is 55% average and 60% peak, as before layers, with
   9 KB of stack never used. Scoped in [docs/layers.md](docs/layers.md).
7. **Maybe:** CC learn, and importing wavetables from the web page. The
   [AKWF bank](#the-akwf-bank) is in already ([docs/akwf.md](docs/akwf.md)).

## Credits

The [AKWF bank](#the-akwf-bank) (tables 64-127) is made from
[AKWF-FREE](https://github.com/KristofferKarlAxelEkstrand/AKWF-FREE), the
Adventure Kid Waveforms by Kristoffer Ekstrand (Adventure Kid), released
under [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/). CC0
asks for no attribution, but the bank wouldn't exist without his work, so
thank you.

The waves in `akwf_bank.cpp` are derived from AKWF's: resampled from 600
to 128 samples, rounded to 8 bits, ordered, aligned and crossfaded, or put
through sync, folding, PWM and squeezing, by `tools/akwf_tables.py` (see
[docs/akwf.md](docs/akwf.md)). The tables' names are ours, after the AKWF
folders and waves they come from.

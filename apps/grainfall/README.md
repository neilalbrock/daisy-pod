# Grainfall

Granular and micro-loop effects, our own take on the Hologram Microcosm: 11
effects with 4 variations each, steered by macro controls, then pitch
modulation, a reverb, a resonant low-pass and the dry/wet mix.

```sh
make APP=grainfall program-dfu
```

## Signal flow

```
in -> Hold (live, or the held loop) -> + feedback -> 22 s buffer
   -> effect voices -> pitch mod -> Space (reverb) -> low-pass
   -> effect volume -> + dry -> limiter -> out
```

Where an effect uses feedback (Haze, Pattern, Warp), part of its output is
recorded back into the buffer, a little darker each time round, so grains are
made of earlier grains and delays repeat.

## Controls

The two knobs are shared across six pages. Press the encoder to change page.
After a page change, a knob does nothing until you move it, so values don't
jump.

| Page (LED 2) | Knob 1 | Knob 2 |
|---|---|---|
| 1 (blue) | Activity: more or less of the effect | Repeats: how long the effect goes on |
| 2 (magenta) | Shape: square, ramp up, triangle, ramp down (quarters of the knob) | Time: subdivision 1/4, 1/2, TAP, 2x, 4x, 8x (sixths of the knob) |
| 3 (yellow) | Filter: low-pass, fully open at max, silent at min | Mix: dry to wet |
| 4 (cyan) | Pitch mod depth | Pitch mod rate, 0.05 - 10 Hz |
| 5 (white) | Filter resonance | Effect volume, noon is unity, max x2 |
| 6 (green) | Space: reverb mix, none to reverb only | Reverb mode: A Bright Room, B Dark Medium, C Large Hall, D Ambient (quarters of the knob) |

| Control | Function |
|---|---|
| Encoder turn | Effect and variation, Mosaic A ... Warp D (wraps round). LED 2 flashes the effect's colour, LED 1 the variation: A red, B yellow, C green, D blue |
| Encoder press | Next page |
| Encoder long press (0.6 s) | Reverse on/off. On the preset page, it saves instead |
| Page 7 (LED 2: the slot) | Presets: the encoder turn selects a slot (see [Presets](#presets)) |
| Button 1 | Tap tempo (quarter notes) |
| Button 1 hold (1 s) | Bypass (LED 1 red) |
| Button 2 | Hold sampler on/off. Hold it for momentary: it turns off when you let go |
| Both buttons, 1 s | Mono in: the left input feeds both channels. LED 1 flashes cyan for on, dark for off |

LED 1 is green when active, blue while Hold is on and cyan while reversed. It
blinks off at the start of each beat.

Time sets the effect's rhythmic step against the beat: TAP is one step per
beat, 2x two, 1/4 one every four beats, and so on.

**Hold** freezes the last step of input (the Time setting, between 0.25 and
2 s) and loops it into the effect in place of the live input, so the effect
keeps working on it. The dry signal stays live, so you can play over it.

**Space** is the Microcosm's reverb, on the effect after pitch mod and
before the filter, so closing the filter silences its tail too; the dry
signal stays clean. It is a plate (after Dattorro) in four modes:

| Mode | Decay | Character |
|---|---|---|
| A Bright Room | 0.9 s | Small and bright |
| B Dark Medium | 1.8 s | Darker, the highs rolled off in and around the tank |
| C Large Hall | 3.5 s | Big and smooth, with a 25 ms predelay |
| D Ambient | 9 s | The biggest, most modulated, with a 60 ms predelay |

Changing mode fades the reverb out, empties it and fades in with the new
one (about 20 ms), so the old tail stops. The reverb isn't recorded into the
buffer, so it never feeds back into the effect.

A limiter on dry + wet holds the output under -1 dBFS. Bypass fades over 5 ms
so it doesn't click, and the buffer keeps recording while bypassed.

## Presets

Eight preset slots, kept in the Seed's flash, so they survive power-off and
reflashing. Every app keeps its own.

- **On the Pod:** page 7 is the preset page. Turning the encoder there
  selects a slot and loads its preset; an empty slot is only selected,
  keeping the sound, ready to save into. A long press on the encoder saves
  the current settings to the slot. The knobs do nothing on this page.
  LED 2 shows the slot: a colour each, dim if empty, blinking off while the
  sound differs from what's saved, and flickering white on a save. Before
  any slot is selected it's dim white.
- **Over MIDI:** program change 45-52 (sent as 44-51, channel 1) selects slot 1-8, where the Microcosm keeps its first two user banks.
- **On the web page:** a button per slot, and Save. Selecting the slot
  already selected reloads it, dropping any changes.

A preset holds the effect and variation, every knob page and reverse. Mono in are saved on their own, a few seconds
after they last change. At power-up they come back, then the preset last
selected. Tempo, Hold and bypass are left as they are.

**Factory reset:** hold both buttons while powering up the Pod (through
the bootloader's 2.5 s wait) until both LEDs flash white, then let go. Or
press Factory reset on the web page. Every preset and saved setting
for this app is erased (the other apps keep theirs) and everything goes
back to its defaults.

## Effects

In encoder order. Program change numbers follow the Microcosm, which orders
them differently.

| Effect | Program change | Description (from the Microcosm manual) |
|---|---|---|
| Mosaic | 21-24 | Overlapping loops at multiple speeds |
| Seq | 17-20 | Short loops rearranged into rhythmic sequences |
| Glide | 13-16 | Short overlapping loops that shift in pitch |
| Haze | 25-28 | Clusters of grains, a wash of sound |
| Tunnel | 29-32 | Cyclic micro-loop drones |
| Strum | 33-36 | Rhythmic chains of recent note onsets |
| Blocks | 9-12 | Rearrangements and bursts of recent notes |
| Interrupt | 5-8 | Glitches that interrupt the dry signal |
| Arp | 1-4 | Recent notes sequenced into arpeggios |
| Pattern | 37-40 | Multi-tap delay rhythms |
| Warp | 41-44 | Delay taps with filters and pitch shift |

### Micro Loop

Loops of the last Time step, locked to the grid. A loop at 2x reads two
steps' worth of audio in one step, and one at 0.5x half a step, so every layer
repeats once per step whatever its speed. **Repeats** sets how long a loop is
kept before fresh audio replaces it: 1 to 32 steps, but never more than about
15 s.

**Mosaic**: overlapping loops at several speeds.

| Control | Effect |
|---|---|
| Activity | How many loopers play, 1 to 4. They take turns to capture |
| Shape | Contour of each pass round the loop |

| Variation | Speeds |
|---|---|
| A | Normal and double (an octave up) |
| B | Normal and half (an octave down) |
| C | All double |
| D | Half, normal, double and quadruple |

**Seq**: steps of recent audio rearranged into an 8-step sequence. Repeats
counts the steps before fresh material replaces it.

| Variation | Activity |
|---|---|
| A | Shuffled slices with rests. Activity varies each slice's low-pass |
| B | Slices bounce between normal and half speed (Activity: how often). At full Activity a soft half-speed pad sits underneath |
| C | Overlapping two-step slices in up to three layers (Activity), with a low-pass sweep over each sequence |
| D | Interlocking layers on the half-step (Activity). At full, a bit-crushed octave-down layer joins |

Shape is the contour of each slice.

**Glide**: two overlapping loops whose speed glides between two settings.

| Control | Effect |
|---|---|
| Activity | Glide rate, one sweep per 16 beats to two per beat |
| Shape | The glide's path: square jumps between the speeds, ramp up rises and drops back, triangle rises and falls, ramp down falls and jumps back |

| Variation | Speeds |
|---|---|
| A | Half to normal |
| B | Double to half |
| C | Normal to double |
| D | One loop glides up while the other glides down, half to double |

### Granules

**Haze**: clusters of grains wash the sound out.

| Control | Effect |
|---|---|
| Activity | Grain density and spread, in stereo and in time |
| Repeats | Feedback: from a dry cloud to a long, regenerating wash |
| Shape | Each grain's contour (soft-edged) |
| Time | How far back grains reach: one step, 50 ms to 2 s |

| Variation | |
|---|---|
| A | Long overlapping grains from a play head moving at half speed: the sound is stretched |
| B | Many short grains scattered at random |
| C | Normal and double-speed grains. With Repeats high, the octaves climb into a shimmer |
| D | Normal and half-speed grains |

**Tunnel**: each new note captures a micro-loop that drones on. Up to four
overlap, and the oldest fades out to make room.

| Control | Effect |
|---|---|
| Activity | Depth of the variation's modifier |
| Repeats | How long drones take to fade, 1 s to 30 s. At the top of the knob they never fade |
| Shape | Contour of each pass round the loop, so square is the smoothest drone and the others pulse at the loop rate |
| Time | Loop length, an eighth of a step (20 ms to 400 ms) |

| Variation | |
|---|---|
| A | Loop lengths slowly compress and stretch |
| B | An octave down, with low-pass sweeps every two beats |
| C | Each drone through a resonant band-pass at its own frequency (300 Hz - 3 kHz) |
| D | Each new note squeezes the loops short, then they relax back |

**Strum**: recent notes are replayed on the Time grid. Notes older than about
15 s are forgotten.

| Control | Effect |
|---|---|
| Activity | Density, from one note every four steps to two a step. In B, how many copies overlap |
| Repeats | How long the repeats go on after you play: they fade over 0.3 s to 12 s, and at the top of the knob never stop |
| Shape | Each note's contour |
| Time | The grid, and the note length (one step, 80 ms to 600 ms) |

| Variation | |
|---|---|
| A | Repeats the most recent note |
| B | Overlapping, slightly detuned copies of the most recent note phase against each other |
| C | Cascades through the last few notes in turn |
| D | As C, with double-speed grains of recent notes mixed in |

### Multidelay

Up to eight delay taps at multiples of the Time step, read straight from the
buffer. The last active tap feeds back, so the whole pattern repeats, a
little darker each time. Changing Time or tempo glides the delay times, like
tape.

| Control | Effect |
|---|---|
| Activity | How many taps play, 1 to 8 |
| Repeats | Feedback from the last tap, from a single pass to nearly endless |
| Shape | Tap levels across the pattern: square even, ramp up swelling, triangle loudest in the middle, ramp down fading |
| Time | The step the tap times are counted in |
| Reverse | Every tap plays backwards, in windows of up to one step (half a second at most) |

**Pattern**: tap rhythms, in steps.

| Variation | Taps |
|---|---|
| A | 1, 2, 3 ... 8: a classic delay. One tap plus Repeats is a plain echo |
| B | Every 3/4 step: dotted |
| C | Every 2/3 step: triplets |
| D | Clusters: 1/4, 1/2, 3/4, then 2, 2 1/4, 2 1/2, then 4, 4 1/4 |

**Warp**: taps half a step apart, each changed on the way out.

| Variation | |
|---|---|
| A | Each tap's low-pass opens as you play harder, the later taps less |
| B | Each tap through a resonant band-pass, rising tap by tap from 300 Hz to 3.4 kHz |
| C | Taps pitch-shifted: +12, +7, -12, +5, +12, -5, -7, -12 semitones. With Repeats up, the last tap's shift builds on itself |
| D | Each tap swaps between itself and an octave up every two beats, fading across |

### Glitch

**Blocks**: each note you play sets off a burst of blocks: half-step slices
of the last two steps, played back rearranged on the grid. Every four blocks,
fresh ones are taken from what you just played. A burst starts on the next
block after the note, opening with its attack (that first block plays
forwards, at no more than normal speed).

| Control | Effect |
|---|---|
| Activity | How far the blocks stray from a steady, repeating order: reshuffles, stutters, reversed blocks and each variation's manipulation |
| Repeats | How long a burst goes on: 2 to 32 half-steps. At the top of the knob it never stops |
| Shape | Each block's contour |
| Time | The grid: a block is half a step (40 ms to 1 s) |

| Variation | |
|---|---|
| A | Bursts of quick runs through the last four notes break in |
| B | Blocks pitch-shifted an octave down, a fifth up or an octave up |
| C | Longer blocks overlap, low-pass filtered and fading out: a softer glitch |
| D | Pitch-shifted and bit-crushed (more crush with Activity) |

**Interrupt**: glitches cut into the live signal, which the effect passes
through between them. The dry signal isn't touched, so at 100% Mix the
glitches truly interrupt your playing. Below that, the live signal is heard
both dry and through the effect between glitches, the two adding up to its
normal level.

| Control | Effect |
|---|---|
| Activity | How much each glitch is chopped up (1 to 4 chops, D 2 to 8) and manipulated; glitches sometimes run half or twice as long |
| Repeats | How often glitches come: a 4% chance each step, up to every step |
| Shape | Each chop's contour |
| Time | The grid, and the glitch length (one step, 60 ms to 1.5 s) |

| Variation | |
|---|---|
| A | Chops of your recent playing, rearranged and sometimes reversed |
| B | Chops pitch-shifted: an octave down, a fourth down, a fifth up, an octave up |
| C | A slice echoing through a band-pass that falls from 4 kHz to 300 Hz |
| D | Bit-crushed chops at drastic speeds (two octaves down to two up), and tiny stutters |

**Arp**: the last few notes you played, sequenced one to a step.

| Control | Effect |
|---|---|
| Activity | Steps in the arpeggio, 2 to 8: the recent notes oldest to newest, round again if there are fewer |
| Repeats | How long it plays on after you play: it fades over 0.3 s to 12 s, and at the top of the knob never stops |
| Shape | Each note's contour |
| Time | The grid: notes are a step apart and long (40 ms to 500 ms) |

| Variation | |
|---|---|
| A | The notes as played |
| B | Each step at its own speed, so the pattern climbs and falls a major arpeggio |
| C | Each step through its own random low-pass or band-pass, new with each note you play |
| D | Each step bit-crushed by its own amount |

## MIDI

Grainfall listens on the Pod's TRS MIDI input and over USB MIDI (the Seed's
USB port shows up as a MIDI device on a computer), on channel 1, with the
Microcosm's map. Send clock to only one of them at a time.

**Clock:** MIDI Start (or Continue) switches to the incoming clock, and Stop
switches back to the internal tempo, which carries on from where the clock
left it. If the clock just stops arriving, it does the same. Tap tempo is
ignored while following clock.

**Program change** 1-44 (sent as 0-43) selects the effect and variation:
four per effect in the order above. 45-52 (sent as 44-51) select presets
1-8, where the Microcosm keeps its first two user banks.

**Control change:**

| CC | Function | Range |
|---|---|---|
| 5 | Time subdivision | 0 = 1/4, 1 = 1/2, 2 = TAP, 3 = 2x, 4 = 4x, 5 = 8x |
| 6 | Activity | 0-127 |
| 7 | Shape | 0-31 square, 32-63 ramp up, 64-95 triangle, 96-127 ramp down |
| 8 | Filter | 0-127 |
| 9 | Mix | 0-127 |
| 10 | Tempo, 40-240 BPM | 0-127 |
| 11 | Repeats | 0-127 |
| 12 | Space | 0-127 |
| 14 | Pitch mod rate | 0-127 |
| 15 | Filter resonance | 0-127 |
| 16 | Effect volume | 0-127 |
| 19 | Pitch mod depth | 0-127 |
| 20 | Reverb mode | 0-31 A Bright Room, 32-63 B Dark Medium, 64-95 C Large Hall, 96-127 D Ambient |
| 47 | Reverse: toggles on 64-127 | |
| 48 | Hold: 0-63 off, 64-127 on | |
| 93 | Tap tempo: taps on 64-127 | |
| 102 | Bypass: 0-63 bypass, 64-127 engage | |

A CC moves the value just as a knob would. If that parameter's page is
showing, its knob lets go until you move it, so it doesn't snap back.

The Microcosm's phrase looper and preset CCs are ignored.

## Web control surface

With the Pod plugged in by USB, `make web` and http://localhost:8000 (Chrome
or Edge) show the buffer with every voice and delay tap as it plays, the beat
grid, detected notes, levels and CPU load, next to all the controls: the
effect grid, the five knob pages, tempo and tap, and the switches (including
mono in). See [web/README.md](../../web/README.md).

The page only streams while it's open, and only over USB.

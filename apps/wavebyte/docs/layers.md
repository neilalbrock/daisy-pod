# Stage 6: layers, scoped

What the roadmap's stage 6 would take, what it would cost the Seed, and
the order to build it in. Written before any of it exists, so the numbers
are measurements of today's firmware plus estimates; each estimate says
what it rests on.

**As built** (see the [README](../README.md#layers) for how it plays): it
followed this scope, with these differences. The arpeggiator and
sequencer (stage 5) are parked, so the questions about them fall away.
Flavour and DAC are shared, as are Master and the effects; there are no
key or velocity ranges. The Layer page's knobs do nothing: its encoder
turn picks the layer, with Shift the mode, and a long press switches the
layer on or off; the split channel is on the web page only, and there's
no "layer A on all channels" option (see below). B-D's parameter names start "B: " rather than "B ", since with a
plain space two pairs hashed alike. There are 413 parameters. Measured
after building: DTCM went from 90.4% to 87.6% with the presets' arrays
and the page's state in SDRAM, then back to 90.6% with the layers, leaving
12.4 KB for the stack (12.6 KB before); and in the host benchmark,
`SetGlobal` and four `SetPatch` come to 4% of the voices and effects'
time, against 1% for one layer before. On the Pod, with all four layers
running and the settings pushed, CPU is 55% average and 60% peak (about
where one layer with effects was) and the stack has 9 KB never used.

**Checked against the manual** (Protein manual rev. 2, firmware 1.02,
read after building): the layers match it. Single plays the selected
layer, the Multi modes (Layered, Round Robin, Random Robin, MIDI Split)
play the active ones, the four layers share eight voices, the FX routing
(Out, FX 1, FX 2) is per layer and the two effects global, and each
layer has its own Poly, Mono or Legato. Its Layer Action menu (Init,
Load, Copy, Paste Layer) is now on the web page. Differences: the
Protein's per-layer output Volume and its sound Volume are one Volume
here; it has a global Omni mode and Receive Channel, where wavebyte
always listens on every channel and the split channel is only MIDI
Split's starting point. The reviews' "layer A on all channels" isn't in
the manual (it seems to be Omni), so the A All Channels toggle built from
it was taken out again. Flavour's "current
sound" could mean per layer; it's global here, by choice.

**Short answer:** yes, it fits. Layers don't add voices: the Protein's
four layers share its eight, so the DSP that dominates the CPU (voices and
effects) stays the same size. What grows is control work (each layer's
patch, once per block, about 2-5% more CPU) and state, and the state is
the real constraint: DTCM is at 90%, and its free 12 KB is the stack. Move
the presets' arrays and the web page's state out of DTCM first and there's
room.

## What the Protein does

I couldn't open the Protein manual itself (this session's network blocks
waldorfmusic.com and the PDF mirrors), so this comes from quotes of the
manual in reviews and forum threads. **Confirmed** means a quote of the
manual; **reported** means a review says so; the rest is open. Worth
checking against the manual's Layer and Multi pages before building.

- **Confirmed:** four layers, A-D, that "share the maximum of 8 voices":
  one pool, no reservation. Layered with all four on, two notes take every
  voice.
- **Confirmed:** Single mode plays the selected layer. Multi mode plays
  them by one of:
  - **Layered:** every active layer plays each note.
  - **Round Robin:** "each incoming note triggers the next active layer in
    ascending order".
  - **Random Robin:** a random active layer per note.
  - **MIDI Split:** each layer on its own channel, from the global receive
    channel up. (The reviews' "layer A can receive on all channels" turned
    out not to be in the manual.)
- **Confirmed:** the two effect slots are global and shared. Each layer
  has its own routing: Out (dry), FX 1 (FX 1 then FX 2) or FX 2 (FX 2
  only). This is wavebyte's routing already, moved into the layer.
- **Confirmed:** each layer has a select button, can be active or not
  (muted), and has Load Layer (from any stored sound), Copy, Paste and
  Init.
- **Reported:** oscillators, filter, envelopes, LFOs, mod matrix, voice
  mode (Poly, Mono, Legato), volume and pan are per layer. A sound holds
  all four layers and the mode; there's no separate multi memory.
- **Open:** whether Flavour, Dirt, DAC, glide, chord and scale are per
  layer (forum posts say Flavour is global); whether the arpeggiator and
  sequencer are per layer or global (sources disagree); whether "Free
  Global" LFOs are global to the layer or to the whole synth (most likely
  the layer); voice stealing across layers; key or velocity ranges (none
  found: Split means split by MIDI channel).

Sources: [Sound On Sound](https://www.soundonsound.com/reviews/waldorf-protein),
[Gearspace](https://gearspace.com/board/electronic-music-instruments-and-electronic-music-production/1458151-waldorf-protein-5.html),
[Synth Anatomy](https://synthanatomy.com/2026/02/waldorf-protein-review-a-modern-desktop-wavetable-synth-with-an-8-bit-soul.html),
[Gearjunkies](https://www.gearjunkies.com/2026/03/gearjunkies-review-waldorf-protein-synthesizer/),
and the manual's mirrors at
[manualslib](https://www.manualslib.com/manual/4185324/Waldorf-Protein.html)
and [Kraft Music](https://files.kraftmusic.com/media/ownersmanual/Waldorf_Protein_User_Manual.pdf).

## Scope

### Engine (`synth_engine`, `voice`)

- A `Layer` per layer holds what `SynthEngine` now holds once: the
  `Patch`, the `Voice::Frame` built from it, the cached glide, DAC and
  envelope settings, the two global LFOs and their clock ratios, the keys
  held (for Mono's highest-key handover), the last note and velocity, the
  layer's mono voice, whether it's active, and its FX routing. About 1 KB
  each.
- Voices stay one pool of eight. Each remembers its layer and renders
  with that layer's `Frame`. Poly takes a voice from the pool as now (free,
  else oldest released, else oldest, across every layer, until the manual
  says otherwise). Mono and Legato keep one voice per layer, taken from
  the pool when the layer starts from silence, instead of always
  `voices_[0]`.
- Notes go to layers by the mode: Single, the selected layer; Layered,
  every active one; Round Robin, the next active one; Random Robin, a
  random active one; MIDI Split, the channel's (layer A on every channel
  if set). A note's layers are kept per note (a 4-bit mask, 128 bytes) so
  its note-off, and the sustain pedal's release, reach the same layers
  whichever came next in the robin.
- The MIDI controllers (wheel, bend, pressure, CCs, MPE's per-channel
  values) stay shared: there's one MIDI stream. MPE and MIDI Split both
  claim the channels, so MPE would be off in Split.
- `SetPatch(layer, patch)` per layer per block, or only for a layer whose
  values changed (see CPU). Pitch bend comes into the frame every block,
  so that part splits off from the rest.
- The probe for the web page follows the last voice played on the layer
  being edited.

### Effects (`fx`)

The voices render into three buses instead of one: into FX 1 (layers
routed FX 1), into FX 2 (routed FX 2) and dry (Out). `FxChain::Process`
runs FX 1 on the first, adds the second, runs FX 2, then adds the dry
bus. A slot runs while any active layer routes through it, and fades out
as now when none does. Types and values stay global, as on the Protein.
The routing leaves the FX page's global choice and becomes the edited
layer's.

### Parameters and presets

- Today's 165 parameters become about 430-450: some 85-90 per layer (the
  knobs but Master, the tables and other choices, Dirt type, Limit,
  Fixed, routing, active) times four, plus the globals (Master, FX types
  and their 72 values, mode, the edited layer, Play, MPE, Tempo). The
  web link's 14-bit indices, its state runs of 256, and `PresetsFor`'s
  limit of 1020 all allow it.
- Layer A keeps today's parameter names, so presets saved now come back
  as layer A, and B-D get a prefix ("B Cutoff"). Keys are 16-bit hashes of
  the names: with about 440 names, about one or two pairs are likely to
  collide (the birthday bound). `static_assert(UniqueKeys(...))` catches
  them at build time; renaming one of the pair fixes it.
- `kParams` is generated in `web_view.cpp` as a `constexpr` list (one
  layer's block repeated with its prefix). It's read-only, so it sits in
  SRAM with the code, not in DTCM.
- A preset holds all four layers and the mode, as on the Protein. Copy,
  Paste and Init are plain value copies in the callback. Load Layer (one
  layer out of another stored preset) needs a small addition to
  `Presets`: read a slot's record in the main loop and apply the
  values of one layer's block, moved to another layer's indices.

### The Pod

- A Layer page, before the preset page. Its encoder turn picks the
  layer the other pages edit (A-D, LED 2 flashing its colour); with Shift,
  the mode. Its knobs: Active for the edited layer, and with Shift, MIDI
  Split's receive channel and whether layer A takes every channel. Copy,
  Paste, Init and Load Layer stay on the web page to start with.
- `PagedKnobs` keeps a set of knob values per layer (4 x 44 knob layers x
  2 knobs, 1.4 KB). Changing the edited layer calls `SetPage`, so the
  knobs do nothing until they move, as after a page change now.
- The CC map sets the edited layer, or in MIDI Split, the layer on the
  message's channel.
- LED feedback for the edited layer is an open question: LED 1 shows the
  amp envelope and Shift, LED 2 the page. One option: LED 2 shows the
  layer's colour for a moment after every page change.

### The web page

Layer tabs (A-D) over today's cards, which then show that layer; the mode,
Active and routing on a Layers card with Copy, Paste, Init and Load Layer;
voices sounding per layer in the telemetry.

## CPU

Measured on the Pod in earlier stages (from the commit messages): with
8 notes and a heavy mod matrix, under 35% at block size 16; with 8 notes
and Reverb plus Chorus or Delay, about 55%.

A host benchmark (`SynthEngine` and `FxChain` compiled for x86, a heavy
patch: Clean, full Flavour, DAC, Dirt, resonance at 0.85, all eight mod
slots busy, one moving an envelope rate) gives these per 16-sample block:

| Work | Host time | Share |
|---|---|---|
| 8 voices | 5.6 us | |
| Chorus + Reverb | 1.4 us | |
| `SetPatch`, one layer | 0.075 us | 1.3% of the voices |

The Pod takes about 26 times as long as the host here (55% of the 333 us
block is 183 us, against 7.0 us on the host). Each extra layer adds a
`SetPatch` and a `MakePatch`. `MakePatch` wasn't measured; it reads about
90 knob values, which I assume costs about what `SetPatch` does. Three extra
layers would then add about 0.45 us on the host, or about 12 us on the
Pod: **3-4% of CPU**, so the worst case goes from about 55% to about 60%.
The Pod's own CPU meter (shown on the web page) should check this at each
step below.

Rebuilding only the layers whose values changed (a dirty flag from
`SetParam`, the knobs, CCs and preset loads, and pitch bend handled per
block on its own) brings that close to zero: a Pod knob moves one layer at
a time.

Nothing else grows much:

- **Voices:** still eight at most, each costing what its own layer's
  settings cost. Layers can't make the voices heavier than today's
  heaviest single patch.
- **Wavetable reads:** each voice reads its own waves now; with layers
  they may come from up to eight tables. The working set (8 voices x 2
  oscillators x up to 2 waves x 128 bytes, 4 KB) still fits the 16 KB
  data cache.
- **Global LFOs:** two per layer, eight in all: a few dozen operations per
  block.
- **Buses:** three stereo buffers of 16 instead of one, and two adds per
  sample.
- **Note dispatch:** per note, not per sample.

What would *not* fit: more voices (each heavy voice is about 4% of CPU, so
16 voices with effects would come close to 95%), or effects per layer
(RAM_D2 holds the reverb, chorus, flanger and look-ahead lines in 205 KB
of its 256 KB, and a second reverb's `Plate` alone needs 176 KB).

## Memory

Measured from today's build (`make APP=wavebyte`, this session's
toolchain was GCC 13.2 rather than 15.3, so sizes may differ slightly):

| Region | Used | Size | Notes |
|---|---|---|---|
| DTCM | 118,512 B | 128 KB | 90.4%. The 12,560 B left is the stack |
| SRAM | 171,964 B | 480 KB | code and read-only data |
| RAM_D2 | 204,672 B | 256 KB | effect lines; malloc grows from the end |
| RAM_D3 | 0 B | 64 KB | nothing maps to it in libDaisy's linker script |
| SDRAM | 1.5 MB | 64 MB | wavetables, delay lines |

DTCM's biggest users: `hw` 37,856 B and `usb_midi` 37,204 B (libDaisy's
two MIDI handlers, each with a 256-event queue), `engine` 5,584 B,
`presets` 3,096 B, `web_state` 660 B, `knobs` 376 B.

The stack serves the main loop, the USB interrupt and the audio interrupt,
which can nest. Its largest frames: `AudioCallback` 880 B, `SendWeb` 688 B
(it holds the page's parameter values, 4 bytes each), `Voice::Render`
192 B, `FxChain::Process` 176 B (from `-fstack-usage`). A rough peak is
4-5 KB, so today's margin is about 8 KB.

What layers add to DTCM, as things are built today:

| | Now | With layers | Added |
|---|---|---|---|
| Engine: 3 more layers (Patch 320 B, Frame 332 B, LFOs, bookkeeping) | 5,584 B | ~8.5 KB | ~2.9 KB |
| Presets' arrays (18 B per parameter) | 3,096 B | ~8.1 KB | ~5.0 KB |
| `web_state` (4 B per parameter) | 660 B | ~1.8 KB | ~1.1 KB |
| Knob values per layer | 376 B | ~1.5 KB | ~1.1 KB |
| `SendWeb`'s copy of the values (stack) | 660 B | ~1.8 KB | ~1.1 KB |
| **Total** | | | **~11 KB of the 12.5 KB free** |

That leaves no stack. So, before layers:

1. **Measure the stack.** Fill free DTCM with a pattern at boot and report
   the high-water mark on the web page, so the margin is a number rather
   than an estimate.
2. **Move the presets' arrays to SDRAM.** They're only touched by the main
   loop, and by `Apply` when a preset is waiting. `Presets` already takes
   its arrays by pointer; a variant whose arrays are plain globals marked
   `DSY_SDRAM_BSS`, zeroed by `Init` (SDRAM isn't running until
   `hw.Init`, so nothing may write it from a static constructor), frees
   3 KB now and 8 KB with layers. It's a `common/` change: `make
   all-apps`.
3. **Move `web_state` and `SendWeb`'s copy to SDRAM** (main loop only).

That brings layers' net cost to about 4 KB of DTCM against about 4 KB
freed now, so the margin stays about where it is today. Kept in reserve
if stage 5 or 7 need more: an app linker script (`app.mk` can set
`LDSCRIPT`) that puts the USB MIDI handler's 37 KB in the unused RAM_D3,
zeroed before the static constructors run.

SRAM (code) grows by the layer logic, a few KB. SDRAM doesn't change.

## Order of work

Each step builds, runs and gets measured (CPU on the web page, stack
high-water mark) before the next:

0. Stack measurement, presets' arrays and `web_state` out of DTCM. No
   change in behaviour.
1. `Layer` in the engine, voices tagged with their layer, Single mode on
   layer A only. Nothing audible changes, and CPU should be unchanged.
2. The parameter list per layer (layer A keeping today's names), presets,
   the Pod's Layer page and edited layer, web page tabs. Single mode plays
   the selected layer.
3. Multi: Layered, Round Robin, Random Robin, MIDI Split; routing per
   layer and the three buses. Measure the worst case here: Layered, all
   four layers on a heavy patch, two notes held (8 voices), Reverb plus
   Chorus.
4. Copy, Paste, Init and Load Layer.

## Questions for you

1. **Stage 5 first?** The roadmap puts the arpeggiator and sequencer
   before layers, and the sources disagree on whether the Protein's are
   per layer. If they are, a 32-step sequencer (pitch and velocity per
   step) and the arpeggiator's settings add about 75 parameters per layer,
   300 in all. That's still within limits (about 750 parameters, under
   1020), but it makes step 0 above essential. Building stage 5 with its
   state in a struct that a layer could own later would keep either answer
   open.
2. Should Flavour, DAC and Clean/8-bit be per layer or global? The forums
   say Flavour is global on the Protein; per layer is more fun and costs
   nothing.
3. Key or velocity ranges per layer: not on the Protein (as far as I could
   find), but cheap to add (two notes and two velocities per layer, and a
   check at dispatch). Keep the quirk, or add them?
4. How should the Pod show which layer is being edited?

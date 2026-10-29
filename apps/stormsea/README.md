# Stormsea

A stereo reverb named after Oceanus Procellarum, the Ocean of Storms on the
Moon: a dark, glassy plain named for weather it never has. With the storm
down it's a clean, lush reverb whose tails run from half a second to
forever. Turn **Storm** up and weather rolls across the tail: wind churns
it, the sea surges and boils, lightning flashes through it with thunder
rolling after, rain falls on it, and an undertow drags it down. **Gusts**
makes the storm come and go on its own. The storm only touches the wet
signal: the dry passes clean.

This is our own design, not modelled on a pedal.

```sh
make APP=stormsea program-dfu
```

**Status: design draft.** Nothing is built yet. This README is the spec to
review; [Roadmap](#roadmap) lists the stages.

Every control is also over MIDI (see [MIDI](#midi)) and on the
[web page](#the-web-page). Eight [presets](#presets) are kept in flash.

## Signal flow

```
in -+---------------------------------------------------------------- dry -+
    |                                                                       |
    +-> gate (on/off, Freeze) -> predelay -> input diffusers                |
        -> the sea: 8 delay lines, each                                     |
             delay (Size; Depth and Wind move it)                           |
             -> damping (Tone) -> [Surge: gain past unity, soft clip]       |
             -> mixing matrix [Undertow: turning, sinking]                  |
             -> sea wall -> back into the lines x Decay                     |
           <- [Lightning and Thunder strike in, Rain falls in]              |
        -> [Wind: whistle] -> Tide (level and position swells) -> Width     |
        -> wet                                                              |
    mix <-------------------------------------------------------------------+
    -> limiter -> out
```

Everything in brackets is the storm, scaled by Storm's current strength.
At Storm 0 none of it runs and the sea is calm.

## The calm sea

The reverb is a feedback delay network: eight delay lines of different
lengths, whose outputs are mixed by a matrix and fed back into them all.
The matrix is orthogonal, so it moves energy between the lines without
adding or losing any. That makes it clean (no metallic ring at long
decays, and dense from the first moment, thanks to the input diffusers),
and it makes an infinite tail hold its level exactly.

| Control | |
|---|---|
| **Decay** | 0.5 s to 60 s, then **∞** over the knob's last stretch. At ∞ the sea keeps everything you play and layers it, under the sea wall |
| **Mix** | Dry to wet, equal power |
| **Size** | The lines' lengths, from a hall to a vast open sea. Moving it while the sea rings bends the tail's pitch, as the waves stretch |
| **Tone** | Damping inside the loop, dark to bright: each trip round loses a little more top. At ∞ and in Freeze it moves to the output instead, so a held sea keeps its colour rather than darkening to a hum |
| **Depth** | Slow, gentle modulation of the lines' lengths, each at its own rate, for a lusher tail with no audible wobble. At 0 it's still and pure |
| **Tide** | The sea's own motion: slow swells in the tail's level (up to about ±4 dB) and stereo position, on a free-running cycle of 10 to 30 s. Tide is the calm sea's, so Storm doesn't scale it |
| **Predelay** | 0, 15, 30, 60, 120 or 240 ms before the sea |
| **Width** | Narrow (the same tail on both sides), medium, or wide (the inputs go into the sea separately and it spreads them) |

The **sea wall** is a slow level control inside the loop that holds the
sea under a ceiling. You only meet it at ∞, or frozen with a storm feeding
in, or with Surge up: it rides the level down gently rather than clipping.
A fixed low cut inside the loop keeps the sea from filling with rumble.

## The storm

**Storm** sets how strong the storm is, from calm to gale. Each element's
own control sets its part in the storm, so a preset can be all wind and
rain, or a thunderstorm, or a swelling, boiling sea: Storm then scales
them all together. At Storm 0 every element is off, whatever its setting.

**Gusts** sets how the storm moves. At 0 it holds steady at Storm's
setting. Turned up, it comes and goes on its own: fronts that build and
pass over tens of seconds, with sudden gusts on top, swinging above and
below Storm's setting, so a medium storm has lulls of near calm and gusts
near full. Gusts runs free, with no tempo, like weather. LED 1 and the web
page show the storm's strength from moment to moment.

| Element | What it does |
|---|---|
| **Wind** | Turbulence: random modulation of every line's length, faster and deeper as the wind rises, so the tail's pitch smears and churns. And the whistle: a resonant band-pass on the tail that sweeps with the gusts, like wind through rigging |
| **Rain** | Drops: tiny grains (2 to 20 ms) of the recent tail, brighter, at a random pitch (unison, a fifth or an octave up) and position, from a few a second to a downpour. The drops fall into the sea as well as being heard, so rain leaves a shimmer in the tail |
| **Lightning** | How often it strikes, at random, from none to about one strike every 2 s in a full storm (more in the gusts). A strike takes the last fraction of a second of the tail and flashes it back into the sea, sharp and bright: see Strike below. LED 1 flashes white |
| **Thunder** | How loud the thunder that follows each strike is. It comes 0.2 to 3 s after its strike: the same slice an octave down, low-passed, swelling and rolling into the sea's low end. Later thunder is further away, so quieter and darker. Without lightning there's no thunder |
| **Surge** | Raises the sea's gain past unity into a soft clipper in each line, so the tail swells up, thickens and boils instead of dying away. When the storm drops the gain falls back under unity and the sea settles to its Decay |
| **Undertow** | Slowly turns the mixing matrix (it stays orthogonal, so an infinite sea stays stable), so the tail's colours shift and churn as energy moves between lines; and sinks the tail by a few cents on each trip round, so long tails slide down into the deep |

**Strike** chooses what a lightning strike plays:

| Strike | |
|---|---|
| Flash | The slice an octave up |
| Glint | The slice reversed |
| Fork | Each strike picks one at random |

## Weathering

Freeze shuts the input out and holds the sea forever. The storm keeps
working on a frozen sea, and what it does stays: lightning, thunder and
rain leave new partials in it, Surge wears it round, Wind and Undertow
smear and turn it. Freeze a chord, raise a storm, and when the calm comes
back the sea isn't the one you froze. Let go of Freeze and it fades away at
the Decay setting.

## Controls

The two knobs are shared across seven pages. Press the encoder to change
page (LED 2 shows which). After a page change a knob does nothing until you
move it, so values don't jump. Turning the encoder sets that page's choice,
if it has one, and LED 2 flashes it for a second.

| Page (LED 2) | Knob 1 | Knob 2 | Encoder turn |
|---|---|---|---|
| 1 Sea (blue) | Decay | Mix | Width: narrow, medium, wide (red, green, blue) |
| 2 Shape (cyan) | Size | Tone | Predelay: 0 to 240 ms (dim to bright) |
| 3 Motion (green) | Depth | Tide | |
| 4 Storm (violet) | Storm | Gusts | |
| 5 Wind and Rain (white) | Wind | Rain | |
| 6 Lightning (yellow) | Lightning | Thunder | Strike: Flash, Glint, Fork (white, pale blue, yellow) |
| 7 Surge (red) | Surge | Undertow | |
| 8 (the slot) | | | Presets: selects a slot (see [Presets](#presets)) |

| Control | Function |
|---|---|
| Encoder press | Next page |
| Encoder long press (0.6 s) | Mono in: the left input feeds both channels. LED 1 flashes cyan for on, dark for off. On the preset page, it saves instead |
| Button 1 tap | On/off. With trails on (the default), turning it off shuts the sea's input and lets the tail ring out under the dry signal; with trails off, the tail is cut in 5 ms |
| Button 1 hold | **Calm**: the storm dies down over a couple of seconds while held, and comes back when you let go |
| Button 2 tap | **Freeze**: holds the sea forever, until you tap again (see [Weathering](#weathering)) |
| Button 2 hold | **Squall**: the storm rises to full in a moment while held, and falls back to its setting over a few seconds when you let go. Frozen or not |

LED 1 is dark while the effect is off. While it's on, it shows the storm's
strength from moment to moment, from a calm deep blue through grey to a
stormy violet, and flashes white on each lightning strike. It glows pale
moonlight white while the sea is frozen (strikes still flash).

Starting values: decay 4 s, mix 35%, size 70%, tone 60%, depth 25%, tide
0, predelay 15 ms, wide. Storm 0, gusts 40%, and the elements part way up
(wind 50%, rain 30%, lightning 30%, thunder 50%, surge 40%, undertow 20%,
Flash), so turning Storm up brings a whole storm at once.

## MIDI

Stormsea listens on every channel, on the Pod's TRS MIDI input and over
USB. A CC that sets a knob's value takes over from the Pod's knob, which
does nothing until you move it again.

| CC | Control | Values |
|---|---|---|
| 14 | Decay | 0-127 (the top of the range is ∞) |
| 15 | Mix | 0-127 |
| 16 | Size | 0-127 |
| 17 | Tone | 0-127 |
| 18 | Depth | 0-127 |
| 19 | Tide | 0-127 |
| 20 | Storm | 0-127 |
| 21 | Gusts | 0-127 |
| 22 | Wind | 0-127 |
| 23 | Rain | 0-127 |
| 24 | Lightning | 0-127 |
| 25 | Thunder | 0-127 |
| 26 | Surge | 0-127 |
| 27 | Undertow | 0-127 |
| 28 | Width | 0-42 narrow, 43-85 medium, 86-127 wide |
| 29 | Predelay | 0-21 0 ms, 22-42 15 ms, 43-63 30 ms, 64-84 60 ms, 85-105 120 ms, 106-127 240 ms |
| 30 | Strike | 0-42 Flash, 43-85 Glint, 86-127 Fork |
| 64 | Sustain pedal | Freeze while down (64-127) |
| 102 | On/off | 0-63 off, 64-127 on |
| 103 | Freeze | 0-63 off, 64-127 on |
| 104 | Mono in | 0-63 off, 64-127 on |
| 105 | Calm | 64-127 while held, like button 1's hold |
| 106 | Squall | 64-127 while held, like button 2's hold |
| 107 | Trails | 0-63 off, 64-127 on |
| 108 | Kill dry | 0-63 off, 64-127 on: no dry at any Mix, for use on a send |

**Program change** 1-8 (sent as 0-7) selects preset slot 1-8.

The choices split 0-127 evenly, so a controller's knob sweeps through them.
LED 2 flashes a choice changed over MIDI only if its page is showing.

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

A preset holds the knobs, Width, Predelay and Strike: the sea and its
weather. Mono in, Trails and Kill dry are saved on their own, a few seconds
after they last change. At power-up they come back, then the preset last
selected. On/off, Freeze, Calm and Squall always start from scratch.

**Factory reset:** hold both buttons while powering up the Pod (through
the bootloader's 2.5 s wait) until both LEDs flash white, then let go. Or
press Factory reset on the web page. Every preset and saved setting
for this app is erased (the other apps keep theirs) and everything goes
back to its defaults.

## The web page

Run `make web` and open http://localhost:8000 in Chrome or Edge with the Pod
connected by USB. The page shows the in, wet and out levels and CPU, and a
view of the sea: its surface rising and falling with the tail's level and
Tide, the sky darkening with the storm's strength from moment to moment,
lightning flashing and thunder rolling across it, and rain falling. A trace
underneath follows the storm's strength over the last minute, so Gusts'
fronts can be seen coming. Every control is there too, with Trails and Kill
dry. See [web/README.md](../../web/README.md).

For Ableton Live, `make m4l` builds a Max for Live device for it: every
parameter as a Live parameter you can automate and map, kept in sync with
the Pod both ways. See [m4l/README.md](../../m4l/README.md).

## Under the hood

- The Seed runs at 480 MHz, with block size 4.
- The sea's eight lines (up to about 150 ms each at the largest Size) and
  the input diffusers are in RAM_D2, through the `.heap` section as
  slowdrift's plate is: the loop reads at least eight times a sample, and
  SDRAM reads are slow. About 160 KB, leaving room for the USB stack's
  heap.
- The predelay and the tail ring that Lightning, Thunder and Rain grab
  from (a few seconds of the wet, stereo) are in SDRAM, read by a handful
  of voices at a time.
- Undertow's turning is a run of small rotations between pairs of lines,
  so the matrix stays orthogonal and an infinite sea can't run away.
  Its sinking is a small pitch shift inside the loop, like slowdrift's
  shimmer turned down.
- Every element is free-running: its own random processes, driven by the
  storm's strength, with no tempo.
- Nothing is cleared in the callback: like the other apps' rings, the
  tail ring counts what's been written and reads silence before it.

## Roadmap

Each stage is proven on the Pod before the next. The calm sea has to sound
beautiful on its own before any weather goes on it.

1. **Next:** the calm sea. The feedback delay network, input diffusers,
   Decay up to ∞, Size, Tone, Depth, Tide, Predelay, Width, the sea wall,
   Mix, on/off with trails, Freeze, mono in, Kill dry, the pages, MIDI,
   presets and a first web page.
2. Wind, Surge, the Storm and Gusts controls, Calm and Squall.
3. Lightning, Thunder and Strike, and Rain, over the tail ring.
4. Undertow, then tuning Storm's curve and the elements' ranges by ear;
   the web page's sea view and the Max for Live device.

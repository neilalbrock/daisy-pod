# Tapeworn

A lo-fi tape machine, after Chase Bliss's Generation Loss MKII: a
deconstructed VHS/cassette deck. Each artifact a tape machine adds has its
own control, so you can have one part badly broken and the rest pristine.

```sh
make APP=tapeworn program-dfu
```

## Signal flow

```
in -> saturate (hysteresis) -> tape transport (wow, flutter, snags, stop)
   -> + hiss, crinkles, pops -> tape model EQ -> drops -> + hum -> volume
                                                      + dry -> limiter -> out
```

## Controls

The two knobs are shared across four pages. Press the encoder to change page.
After a page change, a knob does nothing until you move it, so values don't
jump.

| Page (LED 2) | Knob 1 | Knob 2 |
|---|---|---|
| 1 (blue) | Wow: slow, wandering pitch drift | Flutter: fast, steady pitch and level quiver |
| 2 (magenta) | Saturate: magnetic hysteresis, subtle to blown out | Failure: crinkles, pops, snags and drops |
| 3 (yellow) | Dry level, none to unity | Wet volume, noon is unity, max x2 |
| 4 (cyan) | Noise: tape hiss and mains hum | Stereo spread: channels wobble and fail independently |

| Control | Function |
|---|---|
| Encoder turn | Tape model (LED 2 flashes the model's colour, white = flat) |
| Encoder press | Next page |
| Encoder long press (0.6 s) | AUX type: stop, fail, filter (LED 1 flashes the new type). On the preset page, it saves instead |
| Page 5 (LED 2: the slot) | Presets: the encoder turn selects a slot (see [Presets](#presets)) |
| Button 1 | Bypass (LED 1: green = active, red = bypassed) |
| Button 1 hold (1 s) | Mono in: the left input feeds both channels. LED 1 flashes cyan for on, dark for off |
| Button 2 | AUX on/off. Hold it for momentary: it turns off when you let go |

LED 1 shows the AUX colour while AUX is on.

| AUX | LED 1 | Effect |
|---|---|---|
| Stop | Blue | Tape stop: the wet signal winds down to a halt over 1 s and spins back up in 0.5 s |
| Fail | Orange | Pushes failure to complete malfunction over 0.3 s |
| Filter | White | Bypasses the tape model over 0.15 s |

Starting values: wow 0.3, flutter 0.3, saturate 0.35, failure 0.15, no dry,
unity volume, a little noise, no spread, VHS 2nd gen.

## Presets

Eight preset slots, kept in the Seed's flash, so they survive power-off and
reflashing. Every app keeps its own.

- **On the Pod:** page 5 is the preset page. Turning the encoder there
  selects a slot and loads its preset; an empty slot is only selected,
  keeping the sound, ready to save into. A long press on the encoder saves
  the current settings to the slot. The knobs do nothing on this page.
  LED 2 shows the slot: a colour each, dim if empty, blinking off while the
  sound differs from what's saved, and flickering white on a save. Before
  any slot is selected it's dim white.
- **Over MIDI:** program change 1-8 (any channel, on the TRS input or USB) recalls preset 1-8, as on Generation Loss; 0 does nothing.
- **On the web page:** a button per slot, and Save. Selecting the slot
  already selected reloads it, dropping any changes.

A preset holds the knobs, the tape model and the AUX type. Mono in are saved on their own, a few seconds
after they last change. At power-up they come back, then the preset last
selected. Bypass and AUX on/off always start from scratch.

**Factory reset:** hold both buttons while powering up the Pod (through
the bootloader's 2.5 s wait) until both LEDs flash white, then let go. Or
press Factory reset on the web page. Every preset and saved setting
for this app is erased (the other apps keep theirs) and everything goes
back to its defaults.

## Tape models

Ordered roughly from cleanest to most decayed. They're imaginary machines, not
measurements of real ones.

| # | Model | Character |
|---|---|---|
| 0 | Flat | No playback EQ. Wow, flutter, saturation, failure and noise still apply |
| 1 | Reel-to-reel | Full range, low head bump |
| 2 | Cassette deck | Slight top-end loss, head bump |
| 3 | Walkman | Scooped mids |
| 4 | VHS hi-fi | 9 kHz roll-off, small presence lift |
| 5 | VHS 2nd gen | Darker, warm low mids |
| 6 | VHS 3rd gen | Dull and boxy |
| 7 | 4-track half speed | Dark, heavy low end |
| 8 | Camcorder | Thin, nasal mic |
| 9 | Dictaphone | Narrow, honky |
| 10 | Answering machine | Telephone band |
| 11 | Toy recorder | Tiny, resonant |
| 12 | Melted | Dark, resonant, smeared |

## Notes

- Wow and flutter, Saturate and Noise are squared or curved so the first half
  of each knob stays subtle. Around 9-10 o'clock on wow is a good tape feel;
  past noon it's warped tape.
- Failure brings in crinkles and pops first, snags from about 15 % and drops
  from about 30 %.
- With spread at zero both channels move together. Turn it up, add some dry,
  and wow becomes a wide chorus; failure becomes a randomised stereo splitter.
- Saturate keeps roughly the same level across its range, so quiet signals get
  louder as it compresses. Past noon the drive eases off so the top end stays
  thick rather than crisp, and past 80 % the bias drifts and it gets lopsided.
- A stereo limiter on dry + wet holds the output under -1 dBFS, so turning the
  dry and wet levels up gets louder until it's held there, rather than clipping.
  Bypass skips it and passes the input straight through.
- The Pod's input is stereo. With a mono source on the left channel only, hold
  button 1 to turn on mono in.

## Web control surface

With the Pod plugged in by USB, `make web` and http://localhost:8000 (Chrome
or Edge) show each channel's tape speed and level over time (wow, flutter,
snags, dropouts and the tape stop), the saturator's hysteresis loop, levels
and CPU load, next to all the controls: the tape model, the four knob pages,
the AUX type and switch, bypass and mono in. See
[web/README.md](../../web/README.md).

Tapeworn listens to USB MIDI only for the page; it has no MIDI CC map.


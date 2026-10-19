# Tapeworn

Lo-fi tape machine, our own take on a deconstructed VHS/cassette deck (after
Chase Bliss's Generation Loss MKII). Each artifact a tape machine adds has its
own control, so you can have one part badly broken and the rest pristine.

```sh
make APP=tapeworn program-dfu
```

## Signal flow

```
in -> saturate (hysteresis) -> tape transport (wow, flutter, snags, stop)
   -> + hiss, crinkles, pops -> tape model EQ -> drops -> + hum -> volume
                                                                    + dry -> out
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
| Encoder turn | Tape model (LED 2 flashes the model's colour, white = off) |
| Encoder press | Next page |
| Encoder long press (0.6 s) | AUX type: stop, fail, filter (LED 1 flashes the new type) |
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

## Tape models

Ordered roughly from cleanest to most decayed. They're imaginary machines, not
measurements of real ones.

| # | Model | Character |
|---|---|---|
| 0 | Off | Unfiltered |
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
  louder as it compresses. Past 80 % the bias drifts and it gets lopsided and
  gnarly.
- The Pod's input is stereo. With a mono source on the left channel only, hold
  button 1 to turn on mono in.

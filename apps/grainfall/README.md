# Grainfall

Granular effects processor.

```sh
make APP=grainfall program-dfu
```

## Controls

Input is recorded continuously into a 10 s buffer in SDRAM and played back as
a cloud of short, windowed grains. The two knobs are shared across three pages.
Press the encoder to change page. After a page change, a knob does nothing
until you move it, so values don't jump.

| Page (LED 2) | Knob 1 | Knob 2 |
|---|---|---|
| 1 (blue) | Grain size, 5 ms - 500 ms | Density, 1 - 100 grains/s |
| 2 (magenta) | Position: how far back grains start, 0 - 2 s | Spray: random extra offset, 0 - 1 s |
| 3 (yellow) | Dry/wet mix | Stereo spread |

| Control | Function |
|---|---|
| Encoder turn | Pitch in semitones (-24 to +24) |
| Encoder press | Next page |
| Encoder long press (0.6 s) | MIDI clock sync on/off |
| Button 1 | Bypass (LED 1: green = active, red = bypassed) |
| Button 2 | Freeze the buffer (LED 1 blue while frozen) |

## MIDI clock sync

Grainfall follows MIDI clock from the Pod's TRS MIDI input or over USB MIDI.
The Seed's USB port shows up as a MIDI device on a computer. Send clock to only
one of them at a time.

Long-press the encoder to turn sync on. While clock is arriving:

- Grains start on a note-division grid locked to the clock. MIDI Start resets
  the grid so the next grain lands on the downbeat.
- Density (page 1, knob 2) selects the division: 1 bar, 1/2, 1/4, 1/8, 1/8T,
  1/16, 1/16T, 1/32, 1/32T, 1/64, 1/128.
- Position (page 2, knob 1) snaps to 0, 1/32, 1/16, 1/8, dotted 1/8, 1/4,
  dotted 1/4, 1/2 or 1 bar. With pitch shifted up, grains start a little
  further back so they don't run into the record head, which moves them off
  the grid.
- Grain size and spray stay in milliseconds.
- LED 1 blinks off at the start of each beat.

If sync is on but no clock is arriving, LED 1 flickers and the effect runs
free on the knob settings. It locks as soon as clock arrives, and falls back
to free running within about 0.3 s of the clock stopping.


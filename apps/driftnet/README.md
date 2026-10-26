# Driftnet

A two-channel micro-looper and spatial effect, after Chase Bliss's MOOD MKII.
The **Wet Channel** is a real-time effect. The **Micro-Looper** is always
listening, so when you switch it on it loops whatever you just played,
like casting a net. The two channels feed each other, and the **CLOCK** sets
the sample rate both run at.

```sh
make APP=driftnet program-dfu
```

MOOD's MIDI map, hidden options and the web page are still to come.

## The CLOCK

CLOCK sets the sample rate of everything except the dry signal. It goes from
2k to 64k in MOOD's steps: 2k, 3k, 4k, 6k, 8k, 12k, 16k, 24k, 32k, 48k
and 64k. The steps alternate fifths and fourths, so every change stays in
harmony. The loop and the delay are a fixed number of samples, so the CLOCK
sets several things at once:

- **Length:** a lower clock means a longer loop and a longer delay. The loop is
  2 s at 64k, 2.7 s at 48k and 65 s at 2k.
- **Pitch:** anything already recorded plays slower and lower when you turn
  the clock down. Overdubs keep the speed they were recorded at.
- **Grit:** there's no anti-alias filter, so low settings alias heavily.

## Controls

The two knobs are shared across three pages, one for each column of the
pedal. Press the encoder to change page (LED 2 shows which). After a page
change a knob does nothing until it is moved, so values don't jump.

| Page | LED 2 | Knob 1 | Knob 2 | Encoder turn |
|---|---|---|---|---|
| 1 Wet | Blue | TIME | MODIFY | Reverb, Delay, Slip |
| 2 Loop | Magenta | LENGTH | MODIFY | Env, Tape, Stretch |
| 3 Global | Yellow | CLOCK: 2k to 64k | MIX: dry and wet, both at full at noon | Routing |

What TIME, LENGTH and the two MODIFYs do depends on the mode; see
[The Wet Channel](#the-wet-channel) and [The Micro-Looper](#the-micro-looper).
Turning the encoder moves the page's three-way toggle, and LED 2 flashes
red, green or blue for left, middle or right. It starts on Delay, Tape and
In.

A limiter holds dry plus wet under -1 dBFS.

| Control | Function |
|---|---|
| Button 1 | Wet Channel on; tap again for off |
| Button 1 hold | Freeze while held |
| Button 2 | Micro-Looper: play the last moments; tap again to go back to listening |
| Button 2 hold | Overdub while held |
| Encoder press | Next page |
| Encoder long press | Mono in: the left input feeds both channels (LED 1 flashes cyan for on, dark for off) |

**LED 1** is green while the Wet Channel is on and white while it's frozen,
with red added while the loop plays (so both on shows yellow). It blinks
off at the start of each pass, and its red flickers while overdubbing.

### Routing

Routing decides what the Wet Channel processes while the loop plays. Turn the
encoder on page 3.

| Position | LED 2 flash | The Wet Channel hears |
|---|---|---|
| In | Red | Your input only; the loop stays clean |
| In + Loop | Green | Your input and the loop |
| Loop | Blue | The loop only; your input stays dry |

A loop sent through the Wet Channel is heard only through it.

## The Micro-Looper

It never stops recording while it's off. Switching it on loops the last
loop's worth, ending the moment you pressed. Switching it off again starts
recording over the loop, oldest first, so you can switch it off and straight
back on to catch new material. For example, run a loop through the Delay,
then switch the looper off and on to record the echoes into it.

While the looper is off, it records the Wet Channel as well as your input,
so trails end up in the loop. Overdubs take only your clean input; anything
else would feed the loop back into itself through the effect.

Switching between the looper's modes keeps the loop. Overdubs are written
wherever the play head is, so anywhere but Tape at 1x they can land
somewhere unexpected.

| Mode | LENGTH | MODIFY |
|---|---|---|
| Env | Slice size, 5 ms to 1 s | Sensitivity: how quietly you can play to hold a slice |
| Tape | Shortens the loop to as little as 1/32, keeping its newest end | Speed: 4x, 2x, 1x or 0.5x, reversed below noon and forward above |
| Stretch | Grain size, 2 ms (grainy) to 1 s (clear, with repeating phrases) | Stretch: frozen at noon, less stretched further out, none at either end (reversed at min) |

**Env mode** cuts the loop into slices and plays it at 1x. While your input
is louder than the threshold, it repeats the slice it's in, until you stop.
Turn MODIFY up and touch the strings lightly to slow the loop to a crawl
without being heard.

**Tape mode** changes speed in octaves. Use it to offset the CLOCK: lower the
clock for grit, then speed the loop back up.

**Stretch mode** plays two overlapping grains at 1x, while the place they
read from moves through the loop more slowly. It's stretched 1.5x, 2x and
then 4x on the way towards noon, where it stops and repeats one grain;
LENGTH then shrinks or grows that moment. Below noon it runs backwards. The
steps follow MOOD's MIDI chart, with the knob moving smoothly between them.
To get to know it, set LENGTH and MODIFY to max, turn MODIFY slowly towards
noon, then turn LENGTH down.

## The Wet Channel

Changing mode fades out, then fades in the new one with nothing in it, so
old echoes don't come back.

| Mode | TIME | MODIFY |
|---|---|---|
| Reverb | Size and decay, 30 ms to 500 ms at 48k | Smear: clean multi-tap at min, reverb at max |
| Delay | Delay time, 1 ms to 2.7 s at 48k | Feedback, holding like a looper at max |
| Slip | Sampling window, 5 ms to 2 s at 48k | Speed in semitones: 1x forward at noon, up to 2x forward, or 1x to 2x reversed below noon |

**Reverb mode:** a dense cluster of echoes, eight taps a side, which the
smear turns into a wash. Each pass of the feedback smears it further.
Moving TIME slides the taps, which bends the pitch of what's sounding.
Freeze holds it, and turning MODIFY up while frozen smears it further.

**Delay mode:** a clean delay. Changing TIME crossfades to the new time
instead of bending the pitch of the echoes. At maximum MODIFY the feedback is
exactly 1, so the repeats hold and pile up like a looper. Turning TIME slowly
while they hold records a stretch into them. Freeze cuts the input and holds
whatever is repeating.

**Slip mode:** samples your input continuously and plays its newest moments
back at another speed. Short windows make it a pitch shifter; long ones
make harmonised phrases that follow behind you. Each window keeps the
speed it started with, so turning MODIFY steps the harmony. Freeze stops
the sampling, so it repeats the same window like a synth note. Try a loop
routed through it, moving MODIFY as you punch it in and out.

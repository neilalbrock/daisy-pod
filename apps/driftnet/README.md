# Driftnet

A two-channel micro-looper and spatial effect, after Chase Bliss's MOOD MKII.
The **Wet Channel** is a real-time effect. The **Micro-Looper** is always
listening, so when you switch it on it loops whatever you just played,
like casting a net. The two channels feed each other, and the **CLOCK** sets
the sample rate both run at.

```sh
make APP=driftnet program-dfu
```

So far the Wet Channel has Delay and the Micro-Looper has Tape. MOOD's
other modes (Reverb, Slip, Env, Stretch), its MIDI map, hidden options and
the web page are still to come.

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
| 1 Wet | Blue | TIME: delay time, 1 ms to 2.7 s at 48k | MODIFY: feedback (holds like a looper at max) | – |
| 2 Loop | Magenta | LENGTH: shortens the loop to as little as 1/32, keeping its newest end | MODIFY: speed, 4x reverse to 4x forward | – |
| 3 Global | Yellow | CLOCK: 2k to 64k | MIX: dry and wet, both at full at noon | Routing |

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
encoder on page 3; LED 2 flashes the position.

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

**Tape mode:** MODIFY sets the speed (4x, 2x, 1x or 0.5x, reverse below noon
and forward above). LENGTH shortens the loop. Overdubs are written wherever
the play head is, so at other speeds they land somewhere unexpected.

## The Wet Channel

**Delay mode:** a clean delay. Changing TIME crossfades to the new time
instead of bending the pitch of the echoes. At maximum MODIFY the feedback is
exactly 1, so the repeats hold and pile up like a looper. Turning TIME slowly
while they hold records a stretch into them. Freeze cuts the input and holds
whatever is repeating.

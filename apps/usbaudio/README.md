# USB audio

The Pod as a USB sound card, and the hardware check for `UsbAudio`
(`common/usb_device/`). The computer sees a stereo, 24-bit, 48 kHz output and
input with no driver needed. Audio from the computer plays out the line out,
and the computer records the line in.

```sh
make APP=usbaudio program-dfu
```

After flashing, the Pod shows up as "Daisy Pod" (on a Mac, in Audio MIDI
Setup). Pick it as the output and input in your DAW or system settings.

## Controls

| Control | Function |
|---|---|
| Knob 1 | Level of the computer's audio at the line out |
| Knob 2 | Line in monitoring level at the line out (direct monitoring) |
| Button 1 | Loopback mode on/off (see below) |
| LED 1 | Green while the computer is playing to the Pod, blue while loopback is on (both: cyan) |
| LED 2 | Red while the computer is recording from the Pod |

## Loopback test

In loopback mode the Pod sends the computer's audio straight back to it,
bypassing the codec. The audio still passes through both of `UsbAudio`'s
buffers at the codec's clock, so this tests both directions and the clock
drift handling. Nothing resamples on the way, so a recording of what the
computer plays should match the source sample for sample, and
`loopback_check.py` checks that it does. It needs Python 3 with numpy.

1. Make a test file: `python3 loopback_check.py make-signal noise.wav`
   (30 minutes of stereo 24-bit noise; `--minutes` changes the length).
2. Press Button 1 so LED 1 shows blue.
3. Set the computer's output volume for the Pod to maximum. The Pod offers no
   volume control of its own, so the computer scales the audio in software,
   and anything below maximum changes every sample.
4. In a DAW at 48 kHz, put `noise.wav` on a track playing to the Pod and
   record the Pod's input on another track at the same time. Turn off
   dither, and leave the track and master faders at 0 dB. Export the
   recording as 24-bit or 32-bit float WAV, without normalising.
5. Run `python3 loopback_check.py compare noise.wav recording.wav`.

The script finds the round-trip delay, then checks every sample. It prints
`PASS` if the recording matches exactly. Otherwise it lists each problem
with its time: corrupted samples, or a slip, where frames were dropped or
repeated. If nothing matches at all, it reports the recording's level
relative to the source, which points at a volume or format setting rather
than at the Pod.

## Other checks

- With loopback off, play audio from the computer with Knob 1 up. It should
  come out of the line out cleanly.
- Record the line in on the computer, and listen for clicks.
- Patch the line out into the line in and play something through both
  directions, as a quick check of the analog side.
- Switch the computer's output away from the Pod and back, a few times, and
  check that playback resumes each time.

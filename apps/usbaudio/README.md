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
| LED 1 | Green while the computer is playing to the Pod |
| LED 2 | Red while the computer is recording from the Pod |

## Testing

- Play audio from the computer with Knob 1 up: it should come out of the line
  out cleanly, with no clicks over a long listen (30 minutes or more catches
  clock drift problems).
- Record the line in on the computer: the take should be click-free too.
- Switch the computer's output away from the Pod and back, a few times, and
  check that playback resumes each time.

# Web control surface

A page that connects to the Pod over USB MIDI (Web MIDI), shows what the app
is doing and gives you every control at once. It works with any app that
speaks the web link protocol (`common/web_link.h`); so far that's grainfall.

```sh
make web    # serves this folder on http://localhost:8000
```

Open http://localhost:8000 in **Chrome, Edge or Opera** (Safari and Firefox
have no Web MIDI), with the Pod plugged in by USB and running the app. Allow
MIDI and SysEx when the browser asks. The page picks the Daisy's MIDI ports by
itself; if not, choose them at the top and press Connect.

Web MIDI only works on `localhost` or https, which is why the page is served
rather than opened as a file. It's plain HTML, CSS and JavaScript modules, with
no build step.

## What it shows

- **Buffer**: the last 2 s, 8 s or all 22 s of the sample buffer, scrolling
  right to left. Each voice is a bar over the slice it plays, placed by its
  stereo position (left at the top) and coloured by speed: teal at normal,
  orange to red faster, blue to violet slower. Its play head points the way
  it's reading. Delay taps are vertical lines; their dots are coloured when a
  tap is pitch-shifted. Detected notes are marked along the top, and the beat
  grid (bars brighter) runs underneath.
- **Tempo**: BPM, whether it comes from tap tempo or MIDI clock, and where
  you are in the bar and the beat.
- **Levels**: input, wet and output peaks.
- **CPU**: the audio callback's average and peak load over the last 30 s.
- **Controls**: built from the parameter list the app sends, grouped as on the
  Pod (grainfall's knob pages in their LED colours). Drag a knob up and down
  (Shift for fine), scroll it, or use the arrow keys. Moving a control on the
  Pod or over MIDI moves it here too.

## How it talks to the Pod

All in SysEx, `F0 7D 01 <type> <payload> F7` (`7D` is the ID set aside for
non-commercial use). The page sends a request when it connects and a
keep-alive every second; the app answers with a hello (its name, buffer
layout, group and meter names), one message per parameter, and then its
state (on change, and every half second) and a telemetry frame every 30 ms,
until the keep-alives stop. So nothing is sent to a DAW unless this page is
open. Settings go back as `set` messages, by parameter index.

The app sends from its main loop, never the audio callback, and each message
stays under the 768 bytes libDaisy's USB MIDI can send at once. The page only
works over USB: the Pod's TRS MIDI is far too slow for the telemetry.

## Files

| File | |
|---|---|
| `index.html`, `css/style.css` | The page |
| `js/main.js` | Wires it together, and redraws every animation frame |
| `js/link.js` | The Web MIDI connection: request, keep-alives, decoding |
| `js/protocol.js` | The protocol, matching `common/web_link.h` |
| `js/controls.js`, `js/knob.js` | The control surface |
| `js/views/buffer.js` | The buffer view |
| `js/views/readouts.js` | Tempo, levels and CPU |
| `js/apps/<app>.js` | Optional extras per app: colours, units, group titles |

## Adding an app

On the firmware side (see grainfall's `web_view.{h,cpp}` and the end of its
`main.cpp`):

1. Describe the parameters as a `weblink::Param` table, and write functions
   to read and set them by index.
2. Feed SysEx from the MIDI handler to `weblink::Session::Receive` in the
   audio callback, and apply the `set` commands it returns.
3. From the main loop, call `Session::Next` every 10 ms or so and send what
   it asks for: the hello, each parameter, the state, or a frame. A frame
   holds whichever sections the app has (tempo, levels, CPU, buffer and
   heads, overview); the page draws the ones it gets.

On the page, add `js/apps/<app>.js` for colours and units if you like; the
plain controls work without it.

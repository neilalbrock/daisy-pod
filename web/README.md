# Web control surface

A page that connects to the Pod over USB MIDI (Web MIDI), shows what the app
is doing and gives you every control at once. It works with any app that
speaks the web link protocol (`common/web_link.h`): grainfall, tapeworn,
driftnet, slowdrift and wavebyte so far. The page shows whichever views the app sends data for.

```sh
make web    # serves this folder on http://localhost:8000
```

Open http://localhost:8000 in **Chrome, Edge or Opera** (Safari and Firefox
have no Web MIDI), with the Pod plugged in by USB and running the app. Allow
MIDI and SysEx when the browser asks. The page picks the Daisy's MIDI ports by
itself; if not, choose them at the top and press Connect.

Web MIDI only works on `localhost` or https, which is why the page is served
(by `serve.py`, which also stops the browser caching it, so a reload picks up
changes) rather than opened as a file. It's plain HTML, CSS and JavaScript
modules, with no build step.

## What it shows

- **Buffer** (grainfall): the last 2 s, 8 s or all 22 s of the sample
  buffer, scrolling right to left. Each voice is a bar over the slice it
  plays, placed by its stereo position (left at the top) and coloured by
  speed: teal at normal, orange to red faster, blue to violet slower. Its
  play head points the way it's reading. Delay taps are vertical lines; their dots are coloured when a
  tap is pitch-shifted. Detected notes are marked along the top, and the beat
  grid (bars brighter) runs underneath.
- **Loop** (driftnet): while the looper listens, the ring it records into,
  scrolling right to left. While it plays, the loop laid out oldest to
  newest, with what's playing it on top: Tape's window, the Env slice being
  held, or Stretch's two grains (fading in and out), each with its play head.
  The panel's head gives the loop's length at the current CLOCK.
- **Clock, Env and ramp** (driftnet): the clock rate, the input level against
  Env's threshold (a slice is held while it's above), and where a ramp or
  bounce is.
- **Tape transport** (tapeworn): each channel's tape speed in cents, so you
  can see wow wander, flutter shimmer, snags jolt and the AUX stop wind down,
  and its level, showing dropouts and the stop's fade. Left is teal, right
  magenta; turn up Spread to see them part.
- **Saturation** (tapeworn): the saturator's input against its output over
  the last 8 ms, the latest few frames fading out. A straight diagonal would
  be clean; the S-curve is the squash and the loop's width is the hysteresis.
  Each axis is scaled to fill the plot, so it shows the shape at any level.
- **LFO, swell and decay** (slowdrift): the modulation LFO, the reverb's
  input gain (Rise's swells, and the gate closing while off or frozen) and
  the decay time, which Sustain, Freeze and Stretch lengthen.
- **Tempo** (grainfall, and driftnet while synced): BPM, whether it comes
  from tap tempo or MIDI clock, and where you are in the bar and the beat.
  Driftnet keeps the panel, marked "Not synced", while it runs free.
- **Presets** (every app but the starter): a button per slot in its LED
  colour, dimmed while empty, with Save and Factory reset (which asks
  first). Clicking a slot loads its preset (an
  empty one is only selected, ready to save into); clicking the selected
  slot again reloads it. The head says whether the sound has changed since.
- **Levels**: input, wet and output peaks (and the loop, for driftnet).
- **CPU**: the audio callback's average and peak load over the last 30 s.
- **Controls**: built from the parameter list the app sends, grouped as on the
  Pod, in three zones the same for every app: a group that is one big choice
  (grainfall's effects, tapeworn's tape models) across the top, the knob pages
  in a row (in their LED colours, numbered, titled by their knobs), and the
  rest in columns. Every card lays out knobs, then choices (segmented
  controls), then switches (toggles with a light, triggers with a ring). Drag
  a knob up and down (Shift for fine), scroll it, or use the arrow keys.
  Moving a control on the Pod or over MIDI moves it here too.

## How it talks to the Pod

All in SysEx, `F0 7D 02 <type> <payload> F7` (`7D` is the ID set aside for
non-commercial use, `02` the protocol version). The page sends a request when it connects and a
keep-alive every second; the app answers with a hello (its name, buffer
layout, group and meter names, and any traces and X-Y plot), the parameters'
descriptions (as many to a message as fit), and then its state (on change, and every half second,
in runs of up to 256 values) and a
telemetry frame every 30 ms, until the keep-alives stop. So nothing is sent to a DAW unless this page is
open. Settings go back as `set` messages, by parameter index, and presets as
`preset select` and `preset save` messages, by slot, and `preset reset`; each frame says which
slot is selected, whether it's modified and which slots hold a preset.

The app sends from its main loop, never the audio callback, and each message
stays under the 768 bytes libDaisy's USB MIDI can send at once. The page only
works over USB: the Pod's TRS MIDI is far too slow for the telemetry.

## Files

| File | |
|---|---|
| `index.html`, `css/style.css` | The page |
| `serve.py` | The local server `make web` runs |
| `js/main.js` | Wires it together, and redraws every animation frame |
| `js/link.js` | The Web MIDI connection: request, keep-alives, decoding |
| `js/protocol.js` | The protocol, matching `common/web_link.h` |
| `js/controls.js`, `js/knob.js` | The control surface |
| `js/presets.js` | The presets card |
| `js/views/buffer.js` | The buffer view |
| `js/views/loop.js` | The loop view, which an app can use instead (driftnet) |
| `js/views/readouts.js` | Tempo, levels and CPU |
| `js/views/traces.js` | Traces over time, and the X-Y plot |
| `js/apps/<app>.js` | Optional extras per app: colours, units, menus, group titles, its own buffer view |

## Adding an app

On the firmware side (see `web_view.{h,cpp}` and the web link section of
`main.cpp` in grainfall or tapeworn; tapeworn is the smaller example):

1. Describe the parameters as a `weblink::Param` table, and write functions
   to read and set them by index.
2. Feed SysEx from the MIDI handler to `weblink::Session::Receive` in the
   audio callback, and apply the `set` commands it returns.
3. From the main loop, call `Session::Next` every 10 ms or so and send what
   it asks for: the hello (`weblink::WriteHello`), the parameters
   (`Session::WriteParams`), the state (`Session::WriteState`), or a frame.
   A frame holds whichever sections the app has (tempo, levels,
   CPU, buffer and heads, overview, traces, X-Y); the page draws the ones it
   gets. Traces are the easiest way to show something new: describe them in
   the hello and send their values, and the page plots them.

On the page, add `js/apps/<app>.js` for colours and units if you like; the
plain controls work without it. Its `menu(name)` can return a list of names
for a continuous parameter that picks from a list (a knob stepping through
it on the Pod, as wavebyte's mod matrix sources do): the page shows a
drop-down, and sends the middle of the chosen item's step, (i + 0.5) / n. It can also give a `View`, a class with the
buffer view's methods (`setLayout`, `addOverview`, `setFrame`, `draw`, and
optionally `status`, a line for the panel's head), to draw the buffer section
its own way under its own `scopeTitle`, as driftnet does with
`js/views/loop.js`. `legend` picks the buffer legend's entries (`speed`,
`tap`, `onset`), and `tempo: true` keeps the Tempo panel for an app that only
sends a tempo some of the time.

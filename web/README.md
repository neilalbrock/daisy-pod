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
itself; if not, choose them under **Setup** at the top and press Connect.

Web MIDI only works on `localhost` or https, which is why the page is served
(by `serve.py`, which also stops the browser caching it, so a reload picks up
changes) rather than opened as a file. It's plain HTML, CSS and JavaScript
modules, with no build step.

## What it shows

The page is a **faceplate**: a light panel with one dark display, laid out so
each app fits a 1440 × 900 window without scrolling (it stacks on narrower
screens). `CLAUDE.md` here has the design system and the layout rules.

- **Header**: the app's name, its presets (a button per slot with a stripe
  in its LED colour, dimmed while empty, a dot while the sound has changed
  since; clicking a slot loads it, clicking the selected slot again reloads
  it, and Save writes to it), the connection, and **Setup** (the MIDI ports,
  Connect, and Factory reset, which asks first). Wavebyte's Layer switch
  (A-D, in the layers' LED colours) sits here too.
- **The display** (dark, top left): a small view of what the app is doing,
  level meters, tempo, CPU, and the latest values of its other traces.
  - **Buffer** (grainfall): the last 8 s of the sample buffer, scrolling right
    to left. Each voice is a bar over the slice it plays, placed by its
    stereo position (left at the top) and coloured by speed: teal at normal,
    orange to red faster, blue to violet slower. Its play head points the way
    it's reading. Delay taps are vertical lines, detected notes are marked
    along the top, and the beat grid runs underneath.
  - **Loop** (driftnet): while the looper listens, the ring it records into;
    while it plays, the loop oldest to newest with what's playing it on top
    (Tape's window, the Env slice, or Stretch's two grains). The head gives
    the loop's length at the current CLOCK; the clock, Env and ramp show as
    numbers.
  - **Tape speed** (tapeworn): each channel's speed in cents (left teal, right
    magenta), beside the **Saturation** plot: the saturator's input against
    its output over the last 8 ms. A straight diagonal would be clean; the
    S-curve is the squash and the loop's width the hysteresis.
  - **LFO & swell** (slowdrift): the modulation LFO and the reverb's input
    gain (Rise's swells, the gate closing while off or frozen); the decay time
    shows as a number.
  - **Last voice** (wavebyte): its wave position, cutoff and amp envelope;
    the voices playing and the stack's margin show as numbers.
  - **Tempo** (grainfall, and driftnet, marked not synced while it runs free),
    **levels** (input, wet and output peaks, and the loop for driftnet) and
    **CPU** (the audio callback's average and its peak over the last second).
- **The feature** (beside the display): the app's main choice as big buttons.
  Grainfall's 44 effect slots are the eleven effects plus a Variation switch
  (A-D); tapeworn's tape models show each one's top end; slowdrift's modes
  say what X does in each. Driftnet has none, so its three MOOD columns and
  its channel switches sit there instead.
- **Pages**: the Pod's knob pages as a row of modules, numbered and coloured
  as on the Pod, titled by their knobs, with the page's encoder choice under
  them.
- **Strip**: small groups (tempo and Tap, the switches, driftnet's channels)
  in one row.
- **Drawer** (driftnet): the deep settings, a tab at a time: Hidden options,
  DIP switches, Ramping, Tempo and Synth Mode.
- **Sections** (wavebyte): a rail under the display picks one section at a
  time (Oscillators, Filter & character, Envelopes, Amp & LFOs, Mod matrix,
  Effects, Layers & play), each showing which Pod pages it covers and
  whether it edits the layer picked in the header or is shared. Envelopes
  draw their shape; the mod matrix is a table of source, amount and target;
  an effect's card shows only while FX 1 or FX 2 runs it.

Every module lays out knobs, then drop-downs, then choices (segmented
controls), then switches (toggles with a light, triggers with a ring). Drag a
knob up and down (Shift for fine), scroll it, or use the arrow keys. Moving a
control on the Pod or over MIDI moves it here too.

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
| `index.html`, `css/style.css` | The page and the faceplate's styles |
| `CLAUDE.md` | The design system and layout rules, for changing the page |
| `serve.py` | The local server `make web` runs |
| `js/main.js` | Wires it together, sets up the display, and redraws every animation frame |
| `js/link.js` | The Web MIDI connection: request, keep-alives, decoding |
| `js/protocol.js` | The protocol, matching `common/web_link.h` |
| `js/controls.js`, `js/knob.js` | The faceplate's controls and their layout |
| `js/presets.js` | The presets, in the header |
| `js/views/buffer.js` | The buffer view |
| `js/views/loop.js` | The loop view, which an app can use instead (driftnet) |
| `js/views/readouts.js` | Tempo, levels, CPU and trace values |
| `js/views/traces.js` | The trace strip and the X-Y plot |
| `js/apps/<app>.js` | Optional extras per app: colours, units, menus, layout hints, its own buffer view |

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

Name the groups as the Pod's pages (`Page 1`, `Page 2` ...) and the page lays
them out as modules; a group that is one choice of more than 8 options
becomes the feature, and small groups (no choices, at most one knob) go in the
strip. That's enough for a plain faceplate.

On the page, add `js/apps/<app>.js` for the rest; every entry is optional
(`CLAUDE.md` has the details):

| Entry | |
|---|---|
| `tagline` | A few words under the name |
| `groupColour(name)`, `optionColour(param, i)`, `ledColour(name)` | The Pod's page, option and LED colours |
| `format(name)` | A function from 0..1 to the knob's reading |
| `groupTitle(name, params)` | A card's title, instead of the default |
| `feature`, `optionHint(param, i)` | The main choice, and a line under each of its options |
| `displayTraces`, `readouts`, `tracesTitle` | The traces the display plots, those it shows as numbers, and its title |
| `View`, `scopeTitle` | A class drawing the buffer its own way (`setLayout`, `addOverview`, `setFrame`, `draw`, optionally `status`), as driftnet's `js/views/loop.js` |
| `tempo: true` | Keep the tempo line for an app that only sends a tempo some of the time |
| `drawer` | Group names for the tabbed drawer |
| `sections`, `groupVisible(name, value)` | A rail of sections (`{ label, groups, note }`), and when a card shows |
| `menu(name)` | A list for a knob that picks from one (a drop-down that sends the middle of the item's step, (i + 0.5) / n), or `true` to show a choice as a drop-down |
| `overrides(name)` | Another choice in the card that's not in use while this one isn't 0 |
| `bipolar(name)` | A slot slider that fills from the middle |
| `tabParam` | A choice whose options are the tabs of `<tab>/<card>` groups (wavebyte's Layer), shown as the switch in the header |

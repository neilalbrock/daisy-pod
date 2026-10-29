# web/CLAUDE.md

Guidance for changing the web control surface (`web/`). Read `README.md` here
first for what the page does and how it talks to the Pod; this file is the
design system and the rules that keep every app's page consistent.

## The brief

The page is a **faceplate**: it should feel like the front of a piece of
hardware, not a dashboard. Every app's page fits a **1440 × 900** window
with **no scrolling**, every wavebyte section and every driftnet drawer tab
included. On narrower screens it stacks into one column and scrolls, with no
horizontal scroll down to 390 px wide. Plain HTML, CSS and JS modules, no
build step, Chrome, Edge and Opera only (Web MIDI).

The layout is generic: it's built from the parameter list the app sends
(`hello` groups and `params`), shaped by the optional hints in
`js/apps/<app>.js`. Don't hand-build a page for one app: add or adjust a
hint, or a rule that works for any app.

## Design system

All tokens are CSS custom properties at the top of `css/style.css`. Use them;
don't add raw colours in rules or JS (the Pod's LED colours in the app files
are the exception, as data).

**The plate** (light): `--plate` page, `--panel` modules, `--raised` buttons,
`--raised-hi` a switch that's on and menus, `--sunk` behind a segmented
control, `--line` / `--line-strong` borders, `--ink` / `--ink-soft` /
`--ink-dim` / `--ink-off` text, `--select` (near black) for whatever is picked,
with `--on-select` white text.

**The display** (dark, one per page, top left): `--scr`, `--scr-ink`,
`--scr-bright`, `--scr-dim`, `--scr-line`. `.display` also sets the variables
the canvas views read (`--wave`, `--grid`, `--grid-strong`, `--text-dim`,
`--accent-ui`, `--onset`, `--tap`, `--series-1..4`). The views take their
colours from **their own canvas** (`getComputedStyle(canvas)`), so they theme
with the panel they're in; keep it that way.

**Type**: Archivo (a variable width font; condensed, 75%) for labels and
names, JetBrains Mono for values, both from Google Fonts with system
fallbacks (the page must still work offline, with the fallbacks). Labels are
small caps-style: 11-13 px, weight 700, `font-stretch: 75%`, letter-spacing
0.1em, uppercase (`.label`, `h2`, `h3`, `.choice-label`, `.knob-label`). The
app's name is 34 px, weight 800, 70% width. Values are mono, 11-12 px (18 px
for a module's current value).

**Colour rules**:

- The Pod's page colours (`groupColour`) are **accents only**: a module's
  3 px top edge, its chip, and its knobs' value arcs. They're deepened a
  little with `color-mix(... black)` to read on the light plate. The chip's
  text colour is picked by luminance (`inkOn` in `controls.js`). A colour that
  would vanish on the plate (white, pale yellow) is swapped for a darker one
  in the app file, with a comment saying what the Pod shows.
- Option colours (`optionColour`: the Pod's LED for each option) show as a
  **dot** in the segment or tile, never as the fill.
- Picked = `--select` near-black fill with white text, everywhere (segments,
  tiles, preset slots, rail, drawer tab marker). Don't introduce other picked
  styles.
- Switch lights: `--led` (red) by default; an app can give a switch its own
  (`ledColour`), as the Pod's LEDs do (driftnet's channels, slowdrift's
  On / Sustain / Freeze).
- No gradients except the meters' level scale; no shadows except the setup
  pop-over and switch glows.

**Sizes**: every button and switch is at least 44 px tall (`--control-h`); a
segment is 40 px inside a 2 px frame (`--segment-h`). Gaps are 12 px
(`--gap`) between modules, 8 px between controls. Radius 12 px for modules
and the display, 8 px for controls. Knobs are 64 px in page modules, 72 px in
the feature column, 56 px in sections and the drawer, 48 px in the strip.

## Components (`js/controls.js`, `css/style.css`)

- **Module** (`.module`): a card with a 3 px accent edge, a head (chip for a
  page's number, the title, optionally its current value), then its parts,
  always in this order: knobs, menus (drop-downs), choices (segmented),
  switches. Page titles are their knobs' names joined by " · ".
- **Knob** (`js/knob.js`): drag up and down (Shift for fine), wheel, arrow
  keys, Home and End. Dark cap, light track, accent arc. Label and value under it.
- **Segmented** (`.segmented`): one control for every choice; `--columns`
  from the parameter.
- **Tiles** (`.tiles`, `.tile`): the feature's options, with an optional hint
  line; `.big` for six or fewer (slowdrift's modes).
- **Toggle** (`.toggle`, a light) and **trigger** (`.trigger`, a ring).
- **Menu** (`.menu`): a `<select>` with its label above.
- **Strip** (`.strip`): small groups in one row, separated by rules.
- **Drawer** (`.drawer`): tabs over one group at a time (ARIA tabs, arrow
  keys move between them, the open tab is remembered per app).
- **Rail** (`.rail`): wavebyte's section list under the display; each shows
  its Pod page colours and whether it edits a layer or is shared. The open
  section is remembered per app.
- **Envelopes**: any group with `<x> Attack/Decay/Sustain/Release` knobs gets
  a drawn shape per envelope.
- **Slots**: any group of `<x> <n> <field>` parameters with three or more
  `n` becomes a table (wavebyte's mod matrix): list fields as drop-downs,
  others as sliders (`bipolar` ones fill from the middle).
- **Display** (`index.html` `#display`, `js/views/*`): the scope canvas (the
  buffer view or the app's `View`, 112 px tall) or the trace strip (the
  `displayTraces`, overlaid, 80 px), the X-Y plot (108 px square), meters (one
  row each), the tempo line, the other traces as numbers, and CPU.

## Layout rules (`Controls.build`)

In order, for each group the app sends:

1. `<tab>/<card>` groups belong to a tab (wavebyte's layers); the
   `tabParam` choice becomes the switch in the header, and only the current
   tab's cards show. `*/<card>` shows with every tab. A tab's parameter names
   may start with `<tab>: `, which the labels drop.
2. **Feature**: `extras.feature`, or else a group that is a single choice of
   more than 8 options. It goes beside the display as tiles; options like
   `Mosaic A` ... `Warp D` split into the names and a Variation switch.
3. With `extras.sections`: a rail of sections, each a grid of the cards
   named in its `groups` (anything unnamed goes in a last "More" section).
   `groupVisible(name, value)` hides a card until it applies.
4. Otherwise: groups named in `extras.drawer` go in the drawer; `Page N`
   groups are the pages row; small groups (no choices or menus, at most one
   knob) go in the strip; anything else is a module after the pages. With no
   feature, the pages and the strip move up beside the display.

Labels drop a first word every member of a group shares when the group's name
already gives it ("Chorus Dry/Wet" in Chorus shows as "Dry/Wet"), but
formats, menus and colours are still looked up by the full name.

## The app files (`js/apps/<app>.js`)

`README.md` lists every entry. The current ones:

- **grainfall**: Effect is the feature (split into effects and variations).
- **tapeworn**: Model is the feature, with each model's top end as its hint;
  the display plots the speeds, and the levels show as numbers.
- **driftnet**: its own loop `View`; no feature, so the three MOOD pages and
  the channels sit beside the display; the drawer holds Hidden options, DIP
  switches, Ramping, Tempo and Synth Mode.
- **slowdrift**: Mode is the feature (big tiles, what X does in each mode).
- **stormsea**: no feature, so its five pages and the switches sit beside
  the display; the decay readout says ∞ while the sea is held (`formatTrace`).
- **wavebyte**: seven sections in the rail, the Layer switch in the header,
  effects shown only while a slot runs them, mod amounts bipolar.

When firmware renames or adds a parameter or group, check its app file: the
hints refer to names.

## Checking a change

There's no test suite. Before calling a layout change done:

- Every app at 1440 × 900: `document.documentElement.scrollHeight` is 900.
  Also every wavebyte section and every driftnet drawer tab.
- At 390 px wide: no horizontal scroll (`scrollWidth` is 390).
- Before a Pod connects: the page shows "Not connected", Setup opens with the
  ports, and the empty display says to plug the Pod in.
- Keyboard: every control reachable with Tab, segments and tiles are buttons
  with `aria-pressed`, the drawer's tabs follow the ARIA tabs pattern, knobs
  are sliders with `aria-valuetext`.
- Text contrast at least 4.5:1 (the grey labels on `--panel` and the dim
  display text on `--scr` pass; check any new pair).

Without a Pod, stub Web MIDI: an init script that sets
`navigator.requestMIDIAccess` to return one input and one output named
"Daisy Seed Built In", whose output answers a request (`F0 7D 02 40 F7`) with
the app's hello, its parameters, its state and then a frame every 30 ms,
each encoded as `js/protocol.js` decodes them. Take the parameter lists from
the app's `web_view.cpp` (for wavebyte, whose list is built in code, compile
`web_view.cpp` with `common/web_link.cpp` on the host and dump `kParams` and
the hello). Run it in Playwright at 1440 × 900 and 390 wide, and screenshot.

## Don't

- Don't change `js/protocol.js` without `common/web_link.{h,cpp}` (see the
  top-level `CLAUDE.md`).
- Don't send from the page more often than `SEND_MS` per knob.
- Don't put state the Pod owns in the page: the Pod's state messages are the
  truth, and every control follows them (`Controls.state`). Only view state
  (the open section, the drawer tab) is the page's own, in `localStorage`.
- Don't add a framework or build step.

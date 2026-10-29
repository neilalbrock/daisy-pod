// Stormsea's touches on the faceplate: the Pod's page colours, the
// choices' LED colours and readable units, and its view of the sea
// (views/sea.js), drawn from its traces. No feature, so the pages and the
// switches sit beside the display. Matches apps/stormsea/main.cpp,
// storm_engine.cpp and its README.

import { SeaView } from "../views/sea.js";

const PAGE_COLOURS = {
  "Page 1": "#4f7dff", // Sea: blue
  "Page 2": "#33d6d6", // Shape: cyan
  "Page 3": "#45d36b", // Motion: green
  "Page 4": "#9a4dff", // Storm: violet
  "Page 5": "#7d8794", // Wind and Rain: white on the Pod
  "Page 6": "#e0a400", // Lightning: yellow
  "Page 7": "#ff4d4d", // Surge: red
};
const WIDTH_COLOURS = ["#ff5a5a", "#45d36b", "#4f7dff"]; // narrow red, medium green, wide blue
// The predelay: cyan on the Pod, dim to bright.
const PREDELAY_COLOURS = ["#0b4a4a", "#126e6e", "#1a8f8f", "#22adad", "#2ac4c4", "#33d6d6"];

const RUMBLE_COLOURS = ["#ff8a1a", "#ff3b1f", "#9a4dff"]; // an octave down amber, two red, reversed violet

const INFINITE = 0.96; // StormEngine::kInfinite: Decay from here up holds the sea
const HELD_DECAY = 999; // web_view.h kHeldDecay: the Decay trace while held

const pct = (v) => `${Math.round(v * 100)}%`;
const seconds = (s) => (s < 10 ? `${s.toFixed(1)} s` : `${Math.round(s)} s`);

const FORMATS = {
  Decay: (v) => (v >= INFINITE ? "∞" : seconds(0.5 * Math.pow(120, v / INFINITE))),
  Size: (v) => `${Math.round(30 * Math.pow(1 / 0.3, v))}%`,
  // Thunder's delay, as sky.cpp sets it: 0.15 s near to 5 s far.
  Distance: (v) => `thunder ${seconds(0.15 * Math.pow(5 / 0.15, v))}`,
};

const OPTION_COLOURS = {
  Width: WIDTH_COLOURS,
  Predelay: PREDELAY_COLOURS,
  Rumble: RUMBLE_COLOURS,
};

export default {
  tagline: "a reverb with a storm on top",
  View: SeaView,
  scopeTitle: "The sea",
  scopeLabel: "The sea: its surface with the tail's level, clouds, rain and lightning with the storm, and the storm's last minute",
  displayTraces: [], // the sea view draws them
  readouts: ["Tide", "Decay"],
  formatTrace: (name, v) => (name === "Decay" ? (v >= HELD_DECAY - 0.5 ? "∞" : seconds(v)) : undefined),
  // On: calm deep blue; Freeze: moonlight (paler on the Pod); Calm and
  // Squall: the storm's ends on LED 1.
  ledColour: (name) => ({ On: "#2d5bff", Freeze: "#8fa6e8", Calm: "#2d5bff", Squall: "#9a4dff" })[name],
  groupColour: (name) => PAGE_COLOURS[name],
  optionColour: (param, index) => OPTION_COLOURS[param]?.[index],
  format: (name) => FORMATS[name] || pct,
};

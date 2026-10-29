// Stormsea's touches on the faceplate: the Pod's page colours, the
// choices' LED colours and readable units. No feature, so the pages and
// the switches sit beside the display. Matches apps/stormsea/main.cpp,
// storm_engine.cpp and its README.

const PAGE_COLOURS = {
  "Page 1": "#4f7dff", // Sea: blue
  "Page 2": "#33d6d6", // Shape: cyan
  "Page 3": "#45d36b", // Motion: green
  "Page 4": "#9a4dff", // Storm: violet
  "Page 5": "#7d8794", // Wind and Surge: white on the Pod
};
const WIDTH_COLOURS = ["#ff5a5a", "#45d36b", "#4f7dff"]; // narrow red, medium green, wide blue
// The predelay: cyan on the Pod, dim to bright.
const PREDELAY_COLOURS = ["#0b4a4a", "#126e6e", "#1a8f8f", "#22adad", "#2ac4c4", "#33d6d6"];

const INFINITE = 0.96; // StormEngine::kInfinite: Decay from here up holds the sea
const HELD_DECAY = 999; // web_view.h kHeldDecay: the Decay trace while held

const pct = (v) => `${Math.round(v * 100)}%`;
const seconds = (s) => (s < 10 ? `${s.toFixed(1)} s` : `${Math.round(s)} s`);

const FORMATS = {
  Decay: (v) => (v >= INFINITE ? "∞" : seconds(0.5 * Math.pow(120, v / INFINITE))),
  Size: (v) => `${Math.round(30 * Math.pow(1 / 0.3, v))}%`,
};

const OPTION_COLOURS = {
  Width: WIDTH_COLOURS,
  Predelay: PREDELAY_COLOURS,
};

export default {
  tagline: "a reverb with a storm on top",
  tracesTitle: "Sea & storm · last 8 s",
  displayTraces: ["Sea", "Storm"], // Tide and the decay show as numbers
  formatTrace: (name, v) => (name === "Decay" ? (v >= HELD_DECAY - 0.5 ? "∞" : seconds(v)) : undefined),
  // On: calm deep blue; Freeze: moonlight (paler on the Pod); Calm and
  // Squall: the storm's ends on LED 1.
  ledColour: (name) => ({ On: "#2d5bff", Freeze: "#8fa6e8", Calm: "#2d5bff", Squall: "#9a4dff" })[name],
  groupColour: (name) => PAGE_COLOURS[name],
  optionColour: (param, index) => OPTION_COLOURS[param]?.[index],
  format: (name) => FORMATS[name] || pct,
};

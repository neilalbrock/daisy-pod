// Grainfall's touches on the generic page: the Pod's page and variation
// colours, readable units, and group titles. Matches apps/grainfall/main.cpp
// and post_fx.cpp.

const PAGE_COLOURS = {
  "Page 1": "#4f7dff", // blue
  "Page 2": "#e15cf0", // magenta
  "Page 3": "#f2b630", // yellow
  "Page 4": "#33d6d6", // cyan
  "Page 5": "#e6e6e6", // white
};
const VARIATION_COLOURS = ["#ff5a5a", "#f2b630", "#45d36b", "#4f7dff"]; // A red, B yellow, C green, D blue

const pct = (v) => `${Math.round(v * 100)}%`;
const hz = (f) => (f >= 1000 ? `${(f / 1000).toFixed(f >= 10000 ? 0 : 1)} kHz` : `${Math.round(f)} Hz`);

const FORMATS = {
  Filter: (v) => (v < 0.03 ? "off" : v > 0.995 ? "open" : hz(20 * Math.pow(1000, v))),
  "Mod rate": (v) => `${(0.05 * Math.pow(200, v)).toFixed(2)} Hz`,
  Resonance: (v) => `Q ${(0.5 * Math.pow(24, v)).toFixed(1)}`,
  Volume: (v) => `×${(2 * v).toFixed(2)}`,
  Tempo: (v) => `${(40 + 200 * v).toFixed(1)} BPM`,
};

export default {
  groupColour: (name) => PAGE_COLOURS[name],
  optionColour: (param, index) => (param === "Effect" ? VARIATION_COLOURS[index % 4] : undefined),
  format: (name) => FORMATS[name] || pct,
};

// Tapeworn's touches on the faceplate: the Pod's page colours, AUX
// colours, readable units, each tape model's top end, and the traces the
// display plots. Its Model is the feature. Matches apps/tapeworn/main.cpp
// and tape_model.cpp.

const PAGE_COLOURS = {
  "Page 1": "#4f7dff", // blue
  "Page 2": "#e15cf0", // magenta
  "Page 3": "#f2b630", // yellow
  "Page 4": "#33d6d6", // cyan
};
const AUX_COLOURS = ["#4f7dff", "#ff7a2e", "#9aa1a7"]; // stop blue, fail orange, filter white (grey on the plate)
// Each model's top end (tape_model.cpp), under its name.
const MODEL_HINTS = ["clean", "16 kHz", "12.5 kHz", "10 kHz", "9 kHz", "6.5 kHz", "4.2 kHz", "5.5 kHz", "7 kHz", "3.8 kHz", "3.2 kHz", "2.6 kHz", "1.8 kHz"];

const pct = (v) => `${Math.round(v * 100)}%`;

const FORMATS = {
  Dry: (v) => pct(v * v), // the knob is squared
  Volume: (v) => `×${(2 * v).toFixed(2)}`,
};

export default {
  tagline: "lo-fi tape machine",
  tracesTitle: "Tape speed · last 8 s",
  displayTraces: ["Speed L", "Speed R"], // the levels show as numbers
  optionHint: (param, index) => (param === "Model" ? MODEL_HINTS[index] : undefined),
  groupColour: (name) => PAGE_COLOURS[name],
  optionColour: (param, index) => (param === "AUX type" ? AUX_COLOURS[index] : undefined),
  format: (name) => FORMATS[name] || pct,
};

// Tapeworn's touches on the generic page: the Pod's page colours, AUX
// colours and readable units. Matches apps/tapeworn/main.cpp.

const PAGE_COLOURS = {
  "Page 1": "#4f7dff", // blue
  "Page 2": "#e15cf0", // magenta
  "Page 3": "#f2b630", // yellow
  "Page 4": "#33d6d6", // cyan
};
const AUX_COLOURS = ["#4f7dff", "#ff7a2e", "#e6e6e6"]; // stop blue, fail orange, filter white

const pct = (v) => `${Math.round(v * 100)}%`;

const FORMATS = {
  Dry: (v) => pct(v * v), // the knob is squared
  Volume: (v) => `×${(2 * v).toFixed(2)}`,
};

export default {
  tracesTitle: "Tape transport",
  groupColour: (name) => PAGE_COLOURS[name],
  optionColour: (param, index) => (param === "AUX type" ? AUX_COLOURS[index] : undefined),
  format: (name) => FORMATS[name] || pct,
};

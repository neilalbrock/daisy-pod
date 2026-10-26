// Slowdrift's touches on the generic page: the Pod's page colours, the
// choices' LED colours and readable units. Matches apps/slowdrift/main.cpp
// and slow_engine.cpp.

const PAGE_COLOURS = {
  "Page 1": "#4f7dff", // blue
  "Page 2": "#e15cf0", // magenta
  "Page 3": "#f2b630", // yellow
  "Page 4": "#33d6d6", // cyan
};
const MODE_COLOURS = ["#8a3dff", "#f5f0a0", "#2f9cff", "#ff3045", "#ff7a1a"]; // Dream, Light, Rain, Dark, Rise
const SHAPE_COLOURS = ["#ff4040", "#ccff33", "#33ff99", "#3366ff", "#cc33ff"]; // Hue(i / 5), as LED 2
const POSITION_COLOURS = ["#ff5a5a", "#45d36b", "#4f7dff"]; // narrow red, medium green, wide blue

const pct = (v) => `${Math.round(v * 100)}%`;

const FORMATS = {
  Decay: (v) => `${(0.3 * Math.pow(20 / 0.3, v)).toFixed(1)} s`,
  Stretch: (v) => `${(48 * Math.pow(2, -2 * v)).toFixed(1)} kHz`,
  Rate: (v) => `${(0.05 * Math.pow(200, v)).toFixed(2)} Hz`,
  Pan: (v) => (Math.abs(v - 0.5) < 0.015 ? "centre" : v < 0.5 ? `wet L ${pct(1 - 2 * v)}` : `wet R ${pct(2 * v - 1)}`),
};

const OPTION_COLOURS = {
  Mode: MODE_COLOURS,
  Waveshape: SHAPE_COLOURS,
  Width: POSITION_COLOURS,
};

export default {
  tracesTitle: "LFO, swell and decay",
  groupColour: (name) => PAGE_COLOURS[name],
  optionColour: (param, index) => OPTION_COLOURS[param]?.[index],
  format: (name) => FORMATS[name] || pct,
};

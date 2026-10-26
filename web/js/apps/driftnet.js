// Driftnet's touches on the generic page: the loop view, the Pod's page
// colours, the toggles' position colours and readable units. Matches
// apps/driftnet/main.cpp, drift_engine.cpp and ramp.cpp.

import { LoopView } from "../views/loop.js";

const PAGE_COLOURS = {
  "Page 1": "#4f7dff", // blue
  "Page 2": "#e15cf0", // magenta
  "Page 3": "#f2b630", // yellow
};
const POSITION_COLOURS = ["#ff5a5a", "#45d36b", "#4f7dff"]; // left red, middle green, right blue
const TOGGLES = ["Wet mode", "Loop mode", "Routing"];

const CLOCK_RATES = [2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64]; // kHz

const pct = (v) => `${Math.round(v * 100)}%`;
const hz = (f) => (f >= 1000 ? `${(f / 1000).toFixed(f >= 10000 ? 0 : 1)} kHz` : `${Math.round(f)} Hz`);

const FORMATS = {
  // The stepped CLOCK (SMOOTH glides between 2k and 64k instead).
  Clock: (v) => `${CLOCK_RATES[Math.round(v * 10)]}k`,
  Tone: (v) => (v > 0.995 ? "open" : hz(800 * Math.pow(25, v))),
  "Level balance": (v) => (Math.abs(v - 0.5) < 0.01 ? "even" : v < 0.5 ? `loop ${pct(2 * v)}` : `wet ${pct(2 * (1 - v))}`),
  "Ramp speed": (v) => `${(0.02 * Math.pow(500, v)).toFixed(2)} Hz`,
  Waveform: (v) => ["triangle", "square", "sine", "random", "smooth random"][Math.round(v * 4)],
  Tempo: (v) => `${(40 + 200 * v).toFixed(1)} BPM`,
};

export default {
  View: LoopView,
  scopeTitle: "Loop",
  legend: ["speed"], // the grains are coloured by speed; no taps or notes
  tempo: true, // keep the Tempo panel while unsynced, so the page doesn't jump
  tracesTitle: "Clock, Env and ramp",
  groupColour: (name) => PAGE_COLOURS[name],
  optionColour: (param, index) => (TOGGLES.includes(param) ? POSITION_COLOURS[index] : undefined),
  format: (name) => FORMATS[name] || pct,
};

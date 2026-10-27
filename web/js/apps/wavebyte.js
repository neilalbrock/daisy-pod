// Wavebyte's touches on the generic page: the Pod's page colours (LED 2,
// Hue(page / 8)) and readable units. Matches apps/wavebyte/main.cpp and
// synth_engine.h, adsr.h and voice_filter.h.

const GROUPS = ["Osc 1", "Osc 2", "Pitch", "Filter", "Env & Glide", "Envelopes", "Amp", "Character"];

// common/colour.h's Hue(), as a CSS colour.
function hue(h) {
  const x = h * 6;
  const c = (v) => Math.round(255 * Math.min(Math.max(v, 0), 1));
  return `rgb(${c(Math.abs(x - 3) - 1)}, ${c(2 - Math.abs(x - 2))}, ${c(2 - Math.abs(x - 4))})`;
}

const pct = (v) => `${Math.round(v * 100)}%`;
const bipolar = (v) => 2 * v - 1;
const seconds = (s) => (s < 1 ? `${Math.round(s * 1000)} ms` : `${s.toFixed(s < 10 ? 2 : 1)} s`);
const hz = (f) => (f >= 1000 ? `${(f / 1000).toFixed(f >= 10000 ? 1 : 2)} kHz` : `${Math.round(f)} Hz`);
const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const noteName = (n) => `${NOTE_NAMES[n % 12]}${Math.floor(n / 12) - 1}`;

const attack = (v) => seconds(0.0005 * Math.pow(20000, v));
const fall = (v) => seconds(0.001 * Math.pow(20000, v));
const wave = (v) => {
  const w = Math.round(v * 63);
  return w >= 61 ? `${w} ${["tri", "square", "saw"][w - 61]}` : `${w}`;
};
const waves = (v) => {
  const w = Math.round(bipolar(v) * 63);
  return w > 0 ? `+${w} waves` : `${w} waves`;
};
const semitones = (v) => {
  const s = Math.round(v * 48) - 24;
  return s > 0 ? `+${s} st` : `${s} st`;
};
const cents = (v) => {
  const c = Math.round(bipolar(v) * 100);
  return c > 0 ? `+${c} ct` : `${c} ct`;
};
const octaves = (v) => {
  const o = bipolar(v) * 8;
  return `${o > 0 ? "+" : ""}${o.toFixed(1)} oct`;
};

const FORMATS = {
  "Wave 1": wave,
  "Wave 2": wave,
  "Env Amnt 1": waves,
  "Env Amnt 2": waves,
  "Vel 1": waves,
  "Vel 2": waves,
  "Semitone 1": semitones,
  "Semitone 2": semitones,
  "Detune 1": cents,
  "Detune 2": cents,
  // With Drive as the filter type, Cutoff is the amount (and Resonance
  // the curve); the page shows the low-pass reading.
  Cutoff: (v) => hz(20 * Math.pow(2, 10 * v)),
  Keytrack: (v) => {
    const p = Math.round(bipolar(v) * 200);
    return p > 0 ? `+${p}%` : `${p}%`;
  },
  "Filter Env": octaves,
  "Filter Vel": octaves,
  Glide: (v) => (v <= 0 ? "off" : seconds(0.002 * Math.pow(2500, v))),
  "Bend Range": (v) => `${Math.round(v * 12)} st`,
  Pan: (v) => (Math.abs(v - 0.5) < 0.01 ? "centre" : v < 0.5 ? `L ${pct(1 - 2 * v)}` : `R ${pct(2 * v - 1)}`),
  "Play Note": (v) => noteName(24 + Math.round(v * 72)),
  // SynthEngine::DacBits: 12 bits just above 0, 6 at full.
  DAC: (v) => (v <= 0 ? "ideal" : `${(12 - 6 * v).toFixed(1)} bit`),
};
for (const env of ["Wave", "Filter", "Amp"]) {
  FORMATS[`${env} Attack`] = attack;
  FORMATS[`${env} Decay`] = fall;
  FORMATS[`${env} Release`] = fall;
}

export default {
  tracesTitle: "Last voice",
  groupColour: (name) => (GROUPS.includes(name) ? hue(GROUPS.indexOf(name) / GROUPS.length) : undefined),
  format: (name) => FORMATS[name] || pct,
};

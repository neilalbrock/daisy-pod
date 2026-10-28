// Wavebyte's touches on the generic page: the Pod's page colours (LED 2,
// Hue(page / 11)) and readable units. Matches apps/wavebyte/main.cpp and
// synth_engine.h, adsr.h, voice_filter.h, lfo.h, mod_matrix.h and
// fx_params.h / fx_effects.h.

const GROUPS = ["Osc 1", "Osc 2", "Pitch", "Filter", "Env & Glide", "Envelopes", "Amp", "Character", "LFOs", "Mod Matrix", "FX"];
// Each effect's card, in the FX page's colour.
const EFFECTS = ["Chorus", "Flanger", "Phaser", "Tremolo", "Drive", "Compressor", "EQ", "Delay", "Reverb"];

// The knobs that pick from a list: SynthEngine::Pick.
const pick = (list) => (v) => list[Math.min(list.length - 1, Math.max(0, Math.floor(v * list.length)))];

// lfo.h
const LFO_SHAPES = ["Sine", "Triangle", "Pulse", "Saw", "Saw Up", "Random", "S & H"];
const LFO_MODES = ["Free Voice", "Free Global", "Clocked"];
const RATIOS = ["4/1", "2/1.", "2/1", "1/1.", "1/1", "1/2.", "1/1T", "1/2", "1/4.", "1/2T", "1/4",
  "1/8.", "1/4T", "1/8", "1/16.", "1/8T", "1/16", "1/32.", "1/16T", "1/32", "1/32T", "1/64"];

// mod_matrix.h
const SOURCES = ["off", "Wave Env", "Filter Env", "Amp Env", "LFO 1", "LFO 1 * MW", "LFO 1 * AT",
  "LFO 2", "LFO 2 * MW", "LFO 2 * AT", "Mod Wheel", "Pitch Bend", "Note PB", "Ch Press", "Poly AT",
  "MPE Y", "Pedal", "Expression", "Breath", ...Array.from({ length: 10 }, (_, i) => `CC ${22 + i}`),
  "Keytrack", "Velocity", "Rel Velo", "Random", "Random Bipol", "Constant"];
const TARGETS = ["off", "Wave 1", "Wave 2", "Pitch", "O1 Pitch", "O2 Pitch", "O1 Vol", "O2 Vol", "Dirt",
  "Cutoff", "Resonance", "Volume", "Pan", "LFO 1 Speed", "LFO 2 Speed",
  ...["Wave", "Filter", "Amp"].flatMap((e) => ["Att", "Dec", "Sus", "Rel"].map((s) => `${e} Env ${s}`))];
// SynthEngine::ModAmount: squared, keeping its sign.
const modAmount = (v) => {
  const a = 2 * v - 1;
  const p = a * Math.abs(a) * 100;
  const t = Math.abs(p) < 10 ? p.toFixed(1) : Math.round(p).toString();
  return p > 0 ? `+${t}%` : `${t}%`;
};

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
  Tempo: (v) => `${Math.round(40 + 200 * v)} BPM`,
};

// The effects (fx_effects.h).
const hzOf = (lo, octaves) => (v) => hz(lo * Math.pow(2, octaves * v));
const msOf = (lo, octaves) => (v) => seconds(lo * Math.pow(2, octaves * v));
const db = (range) => (v) => {
  const d = (2 * v - 1) * range;
  return `${d > 0 ? "+" : ""}${d.toFixed(1)} dB`;
};
const signedPct = (v) => {
  const p = Math.round((2 * v - 1) * 100);
  return p > 0 ? `+${p}%` : `${p}%`;
};
const degrees = (v) => `${Math.round(180 * v)}°`;
const q = (v) => (0.3 * Math.pow(2, 5 * v)).toFixed(2);
Object.assign(FORMATS, {
  "Chorus Depth": (v) => `${(6 * v).toFixed(1)} ms`,
  "Chorus Speed": hzOf(0.05, 8),
  "Chorus Feedback": signedPct,
  "Chorus Center": degrees,
  "Chorus High-Cut": hzOf(20000, -6.06),
  "Chorus Low-Cut": hzOf(20, 6.64),
  "Flanger Speed": hzOf(0.02, 9),
  "Flanger Feedback": signedPct,
  "Flanger Center": (v) => `${(0.3 + 9.7 * v).toFixed(1)} ms`,
  "Flanger Phase Diff": degrees,
  "Phaser Speed": hzOf(0.02, 9),
  "Phaser Feedback": signedPct,
  "Phaser Center": hzOf(100, 6.32),
  "Phaser Depth": (v) => `±${(3 * v).toFixed(1)} oct`,
  "Phaser Phase Diff": degrees,
  "Tremolo Speed": hzOf(0.1, 7),
  "Tremolo Phase Diff": degrees,
  "Tremolo Smooth": (v) => `${Math.round(40 * v)} ms`,
  "Drive Gain": db(12),
  "Compr Ratio": (v) => `${(1 + 19 * v * v).toFixed(1)}:1`,
  "Compr Threshold": (v) => `${(-40 * (1 - v)).toFixed(1)} dB`,
  "Compr InGain": db(12),
  "Compr OutGain": db(12),
  "Compr Attack": msOf(0.0001, 9.966),
  "Compr Release": msOf(0.01, 6.644),
  "Compr Look Ahead": (v) => `${(10 * v).toFixed(1)} ms`,
  "EQ Gain": db(12),
  "EQ Low Freq": hzOf(20, 5.64),
  "EQ Low Gain": db(18),
  "EQ Mid Freq": hzOf(100, 6.32),
  "EQ Mid Gain": db(18),
  "EQ Mid Q": q,
  "EQ Mid 2 Freq": hzOf(200, 5.91),
  "EQ Mid 2 Gain": db(18),
  "EQ Mid 2 Q": q,
  "EQ High Freq": hzOf(1000, 4),
  "EQ High Gain": db(18),
  "Delay Left": msOf(0.001, 10.97),
  "Delay Right": msOf(0.001, 10.97),
  "Delay HighCut": hzOf(20000, -5.32),
  "Delay LowCut": hzOf(20, 6.64),
  "Reverb Time": msOf(0.3, 6),
  "Reverb Color": (v) => (Math.abs(v - 0.5) < 0.01 ? "neutral" : v < 0.5 ? `dark ${pct(1 - 2 * v)}` : `thin ${pct(2 * v - 1)}`),
  "Reverb Mod Rate": hzOf(0.1, 6),
  "Reverb Pre-Delay": (v) => `${Math.round(60 * v)} ms`,
});
for (const n of [1, 2]) {
  FORMATS[`LFO ${n} Speed`] = (v) => {
    const f = 0.02 * Math.pow(2, 11 * v);
    return f < 1 ? `${f.toFixed(2)} Hz` : `${f.toFixed(1)} Hz`;
  };
  FORMATS[`LFO ${n} Shape`] = pick(LFO_SHAPES);
  FORMATS[`LFO ${n} Ratio`] = pick(RATIOS);
  FORMATS[`LFO ${n} Mode`] = pick(LFO_MODES);
}
for (let n = 1; n <= 8; n++) {
  FORMATS[`Mod ${n} Source`] = pick(SOURCES);
  FORMATS[`Mod ${n} Target`] = pick(TARGETS);
  FORMATS[`Mod ${n} Amount`] = modAmount;
}
for (const env of ["Wave", "Filter", "Amp"]) {
  FORMATS[`${env} Attack`] = attack;
  FORMATS[`${env} Decay`] = fall;
  FORMATS[`${env} Release`] = fall;
}

// fx_params.h
const FX_TYPES = ["Off", ...EFFECTS];
const DELAY_RATIOS = ["1/1", "1/2.", "1/1T", "1/2", "1/4.", "1/2T", "1/4", "1/8.", "1/4T", "1/8",
  "1/16.", "1/8T", "1/16", "1/32.", "1/16T", "1/32", "1/32T", "1/64"];
const SINE_TRI = ["Sine", "Triangle"];
const OFF_ON = ["Off", "On"];

// The knobs that pick from a list show as menus on the page.
const MENUS = {
  "FX 1 Type": FX_TYPES,
  "FX 2 Type": FX_TYPES,
  "Chorus Shape": SINE_TRI,
  "Chorus Stages": ["2", "4", "6", "8"],
  "Flanger Shape": SINE_TRI,
  "Phaser Shape": SINE_TRI,
  "Phaser Stages": ["2", "4", "8", "12", "16"],
  "Phaser Model": ["Nave", "PPG"],
  "Tremolo Shape": ["Sine", "Triangle", "Square"],
  "Drive Type": ["PNP", "Tube", "PickUp", "Diode", "Crunch"],
  "Compr Auto Gain": OFF_ON,
  "Delay Link": OFF_ON,
  "Delay Sync": OFF_ON,
  "Delay Ratio Left": DELAY_RATIOS,
  "Delay Ratio Right": DELAY_RATIOS,
};
for (const n of [1, 2]) {
  MENUS[`LFO ${n} Shape`] = LFO_SHAPES;
  MENUS[`LFO ${n} Ratio`] = RATIOS;
  MENUS[`LFO ${n} Mode`] = LFO_MODES;
}
for (let n = 1; n <= 8; n++) {
  MENUS[`Mod ${n} Source`] = SOURCES;
  MENUS[`Mod ${n} Target`] = TARGETS;
}

export default {
  menu: (name) => MENUS[name],
  tracesTitle: "Last voice",
  groupColour: (name) =>
    GROUPS.includes(name)
      ? hue(GROUPS.indexOf(name) / GROUPS.length)
      : EFFECTS.includes(name)
        ? hue(GROUPS.indexOf("FX") / GROUPS.length)
        : undefined,
  format: (name) => FORMATS[name] || pct,
};

// Daisy display - the [v8ui] in every designed device: a small screen of
// what the Pod is doing, drawn from what daisy_link.mjs sends it
//
// The node script sends "display" with its state as JSON twenty times a
// second while something changes: the status, the Pod's streams (traces
// over the last 8 s with their latest values, the buffer, tempo, levels,
// the saturator's loop) and the values the layout names (a knob 0..1, a
// choice as its index and text). The JSON comes as numbers, three 7-bit
// characters to each (packText in link_core.mjs), as a string would stay in
// Max's symbol table for good. Each app has its picture; every screen has a
// title line, and the status and preset along the bottom. Everything else
// the node script says comes here too, and is ignored.

autowatch = 0;
inlets = 1;
outlets = 0;

mgraphics.init();
mgraphics.relative_coords = 0;
mgraphics.autofill = 0;

var S = null; // the last state

const INK = [0.86, 0.87, 0.88];
const DIM = [0.55, 0.57, 0.6];
const FAINT = [0.3, 0.31, 0.33];
const BG = [0.075, 0.08, 0.085];
const ACCENT = {
  grainfall: [0.95, 0.71, 0.19],
  tapeworn: [0.91, 0.53, 0.35],
  driftnet: [0.2, 0.84, 0.84],
  slowdrift: [0.56, 0.65, 1.0],
  stormsea: [0.31, 0.62, 1.0],
  wavebyte: [0.27, 0.83, 0.42],
};

function display() {
  const codes = [];
  for (let i = 0; i < arguments.length; i++) {
    const n = arguments[i];
    codes.push((n >> 14) & 127, (n >> 7) & 127, n & 127);
  }
  while (codes.length && codes[codes.length - 1] === 0) codes.pop();
  let json = "";
  for (let i = 0; i < codes.length; i += 4096) json += String.fromCharCode.apply(null, codes.slice(i, i + 4096));
  try {
    S = JSON.parse(json);
  } catch (e) {
    return;
  }
  mgraphics.redraw();
}

function anything() {}

// --- Drawing helpers

function colour(c, a) {
  mgraphics.set_source_rgba(c[0], c[1], c[2], a === undefined ? 1 : a);
}

function text(x, y, s, size, c, bold, align) {
  mgraphics.select_font_face(bold ? "Arial Bold" : "Arial");
  mgraphics.set_font_size(size);
  const w = mgraphics.text_measure(s)[0];
  if (align === "right") x -= w;
  else if (align === "center") x -= w / 2;
  colour(c);
  mgraphics.move_to(x, y);
  mgraphics.show_text(s);
  return w;
}

function fitText(x, y, s, size, c, bold, maxW) {
  mgraphics.select_font_face(bold ? "Arial Bold" : "Arial");
  let t = s;
  mgraphics.set_font_size(size);
  while (t.length > 1 && mgraphics.text_measure(t)[0] > maxW) t = t.slice(0, -1);
  if (t !== s) t = t.slice(0, -1) + "…";
  return text(x, y, t, size, c, bold);
}

function rect(x, y, w, h, c, a) {
  colour(c, a);
  mgraphics.rectangle(x, y, w, h);
  mgraphics.fill();
}

function frameRect(x, y, w, h, c, a) {
  colour(c, a);
  mgraphics.set_line_width(1);
  mgraphics.rectangle(x + 0.5, y + 0.5, w - 1, h - 1);
  mgraphics.stroke();
}

function line(points, c, a, width) {
  if (points.length < 2) return;
  colour(c, a);
  mgraphics.set_line_width(width || 1);
  mgraphics.move_to(points[0][0], points[0][1]);
  for (let i = 1; i < points.length; i++) mgraphics.line_to(points[i][0], points[i][1]);
  mgraphics.stroke();
}

function hdots(x0, x1, y, c) {
  for (let x = x0; x <= x1; x += 3) rect(x, y, 1, 1, c || FAINT);
}

// A lit chip, or a dim one.
function chip(x, y, s, on, accent) {
  mgraphics.select_font_face("Arial Bold");
  mgraphics.set_font_size(8.5);
  const w = Math.ceil(mgraphics.text_measure(s)[0]) + 8;
  if (on) rect(x, y, w, 12, accent);
  else frameRect(x, y, w, 12, FAINT);
  text(x + 4, y + 9, s, 8.5, on ? BG : DIM, true);
  return w;
}

function chips(x, y, list, accent, maxX) {
  for (let i = 0; i < list.length; i++) {
    const [s, on] = list[i];
    mgraphics.select_font_face("Arial Bold");
    mgraphics.set_font_size(8.5);
    if (maxX && x + mgraphics.text_measure(s)[0] + 8 > maxX) break;
    x += chip(x, y, s, on, accent) + 3;
  }
}

// A waveform from the middle: columns as [x, 0..1], scaled up so the
// loudest fills the height (at most 4x, so a quiet buffer still looks
// quiet and silence stays flat).
function wave(columns, mid, half) {
  let peak = 0;
  for (const [, v] of columns) peak = Math.max(peak, v);
  const gain = peak > 0 ? Math.min(4, 0.9 / peak) : 1;
  for (const [x, v] of columns) {
    const hh = Math.max(0.5, v * gain * half);
    rect(x, mid - hh, 1, hh * 2, DIM);
  }
}

// A trace's points (0..1 of its range) across a box, now at the right, so
// a trace still filling grows from there.
function tracePoints(t, x, y, w, h, lo, hi) {
  if (!t || !t.points.length) return [];
  const pts = t.points;
  const n = Math.max(2, S.traceLength || pts.length);
  const a = lo === undefined ? 0 : lo;
  const b = hi === undefined ? 1 : hi;
  return pts.map((v, i) => [x + w - ((pts.length - 1 - i) / (n - 1)) * w, y + h - ((Math.min(b, Math.max(a, v)) - a) / (b - a)) * h]);
}

// The same, scaled to the points' own spread (at least a tenth of the range).
function fitted(t, x, y, w, h) {
  if (!t || !t.points.length) return [];
  let lo = Math.min(...t.points);
  let hi = Math.max(...t.points);
  if (hi - lo < 0.1) (lo = Math.max(0, (lo + hi) / 2 - 0.05)), (hi = lo + 0.1);
  return tracePoints(t, x, y, w, h, lo, hi);
}

// A number 0..1 fixed for each i, and a wrap into 0..1.
function hash(i) {
  const s = Math.sin(i * 12.9898) * 43758.5453;
  return s - Math.floor(s);
}

function wrap(v) {
  return v - Math.floor(v);
}

function mod(x, n) {
  return ((x % n) + n) % n;
}

function trace(name) {
  if (!S || !S.traces) return null;
  for (let i = 0; i < S.traces.length; i++) if (S.traces[i].name === name) return S.traces[i];
  return null;
}

function last(name) {
  const t = trace(name);
  if (!t || t.now === null || t.now === undefined) return null;
  return t.min + t.now * (t.max - t.min);
}

function value(name) {
  return S && S.values && S.values[name] ? S.values[name] : null;
}

function on(name) {
  const v = value(name);
  return !!(v && v.v);
}

function choice(name, fallback) {
  const v = value(name);
  return v && v.text !== undefined && v.text !== null ? v.text : fallback || "";
}

// --- The frame every screen shares

function paint() {
  const [w, h] = mgraphics.size;
  rect(0, 0, w, h, BG);
  if (!S) return void text(w / 2, h / 2 + 3, "Starting", 10, DIM, true, "center");
  const accent = ACCENT[S.app] || INK;
  const box = { x: 6, y: 6, w: w - 12, h: h - 12 };

  // Status and preset, along the bottom.
  const p = S.presets;
  const preset = p && p.slot >= 0 ? "P" + (p.slot + 1) + (p.modified ? "*" : "") : "";
  if (preset) text(box.x + box.w, box.y + box.h, preset, 9, INK, true, "right");
  const status = S.live && S.cpu ? "CPU " + S.cpu.avg + "%" : S.status || "";
  fitText(box.x, box.y + box.h, status, 8.5, DIM, false, box.w - (preset ? 24 : 0));
  hdots(box.x, box.x + box.w, box.y + box.h - 12);

  if (!S.live) {
    text(w / 2, h / 2 - 4, S.app, 13, INK, true, "center");
    text(w / 2, h / 2 + 10, "waiting for the Pod", 9, DIM, false, "center");
    return;
  }
  const area = { x: box.x, y: box.y, w: box.w, h: box.h - 16, accent };
  const draw = APPS[S.app];
  if (draw) draw(area);
}

// --- Each app's picture

const APPS = {
  // The effect, tempo and beat, then the buffer: the last few seconds by
  // age (now at the right), each voice's slice boxed with its play head.
  grainfall(a) {
    fitText(a.x, a.y + 11, choice("Effect", "—"), 13, INK, true, a.w - 44);
    const tempo = S.tempo;
    if (tempo) {
      text(a.x + a.w, a.y + 9, tempo.bpm.toFixed(1), 9, INK, true, "right");
      const beat = Math.floor(tempo.beat) % 4;
      for (let i = 0; i < 4; i++) {
        const x = a.x + a.w - 4 * 7 + i * 7 + 2;
        if (i === beat) rect(x, a.y + 13, 5, 3, a.accent);
        else rect(x, a.y + 14, 5, 1, DIM);
      }
    }
    const top = a.y + 22;
    const bottom = a.y + a.h - 20; // onsets under it, above the chips
    const mid = (top + bottom) / 2;
    const half = (bottom - top) / 2;
    const N = S.bufferFrames;
    const ov = S.overview;
    const buf = S.buffer;
    if (N && ov && buf) {
      const span = Math.min(N, 48000 * 8);
      const ageAt = (x) => (1 - (x - a.x) / a.w) * span;
      const xOf = (age) => a.x + a.w * (1 - age / span);
      const columns = [];
      for (let x = a.x; x < a.x + a.w; x += 2) {
        const f = (((buf.write - ageAt(x)) % N) + N) % N;
        columns.push([x, ov[Math.floor(f / (N / ov.length))] / 127]);
      }
      wave(columns, mid, half);
      const ageOf = (f) => (((buf.write - f) % N) + N) % N;
      for (const o of buf.onsets || []) {
        const age = ageOf(o);
        if (age < span) rect(xOf(age), bottom + 2, 1, 3, INK);
      }
      for (const v of buf.heads || []) {
        const s = ageOf(v.start);
        if (s > span) continue;
        const x0 = xOf(s);
        const x1 = Math.min(a.x + a.w, x0 + (v.length / span) * a.w);
        const hh = Math.max(4, v.amp * half);
        frameRect(x0, mid - hh, Math.max(3, x1 - x0), hh * 2, a.accent, 0.9);
        const into = (((v.pos - v.start) % v.length) + v.length) % v.length;
        const px = x0 + (into / span) * a.w;
        rect(px, mid - hh, 1, hh * 2, a.accent);
      }
    } else hdots(a.x, a.x + a.w, mid);
    chips(a.x, a.y + a.h - 12, [["Time " + choice("Time", ""), false], ["Reverse", on("Reverse")], ["Hold", on("Hold")]], a.accent, a.x + a.w);
  },

  // The machine, tape speed as it wanders (left and right), and the
  // saturator's loop.
  tapeworn(a) {
    fitText(a.x, a.y + 11, choice("Model", "—"), 13, INK, true, a.w);
    const sq = 54;
    const sx = a.x + a.w - sq;
    const top = a.y + 22;
    const pw = a.w - sq - 8;
    // Speed, ±50 cents across the box.
    text(a.x, top + 7, "Speed", 8.5, DIM, false);
    const sh = 40;
    const sy = top + 10;
    hdots(a.x, a.x + pw, sy + sh / 2);
    const lo = 0.5 - 50 / 400;
    const hi = 0.5 + 50 / 400;
    line(tracePoints(trace("Speed R"), a.x, sy, pw, sh, lo, hi), DIM, 1, 1);
    line(tracePoints(trace("Speed L"), a.x, sy, pw, sh, lo, hi), a.accent, 1, 1.5);
    const cents = last("Speed L");
    if (cents !== null) text(a.x + pw, top + 7, (cents >= 0 ? "+" : "") + cents.toFixed(0) + " c", 8.5, INK, true, "right");
    // Level, as a strip.
    const ly = sy + sh + 8;
    text(a.x, ly + 7, "Level", 8.5, DIM, false);
    line(tracePoints(trace("Level L"), a.x + 30, ly, pw - 30, 10, 0.6, 1), INK, 0.8, 1);
    // The saturator's loop.
    frameRect(sx, top, sq, sq, FAINT);
    rect(sx + sq / 2, top + 2, 1, sq - 4, FAINT);
    rect(sx + 2, top + sq / 2, sq - 4, 1, FAINT);
    if (S.xy && S.xy.length) line(S.xy.map(([x, y]) => [sx + sq / 2 + x * (sq / 2 - 3), top + sq / 2 - y * (sq / 2 - 3)]), a.accent, 1, 1.2);
    text(sx + sq / 2, top + sq + 10, "Saturation", 8, DIM, false, "center");
    chips(a.x, a.y + a.h - 12, [["AUX " + choice("AUX type", ""), on("AUX")]], a.accent, a.x + a.w);
  },

  // The modes, the looper (as the web page's loop view: while it listens,
  // the ring it records into, now at the right; while it plays, the loop,
  // oldest first, with what plays it boxed with its play head), the
  // envelope's input against its threshold, and the clock.
  driftnet(a) {
    const wet = choice("Wet mode", "—");
    const w = text(a.x, a.y + 11, wet, 13, INK, true);
    text(a.x + w + 6, a.y + 11, "→ " + choice("Loop mode", ""), 10, a.accent, true);
    const clock = last("Clock");
    if (clock !== null) text(a.x + a.w, a.y + 9, (clock < 10 ? clock.toFixed(1) : clock.toFixed(0)) + " kHz", 8.5, INK, true, "right");
    text(a.x, a.y + 24, choice("Routing", ""), 8.5, DIM, false);
    const top = a.y + 30;
    const bottom = a.y + a.h - 34;
    const mid = (top + bottom) / 2;
    const half = (bottom - top) / 2;
    const ov = S.overview;
    const heads = (S.buffer && S.buffer.heads) || [];
    const loop = heads[0]; // the loop itself, with the clock as its rate
    if (ov && loop) {
      const N = S.bufferFrames || ov.length;
      const listening = !(loop.length > 0);
      const span = listening ? N : loop.length;
      const first = listening ? S.buffer.write - N : loop.start;
      const xOf = (f) => a.x + (mod(f - first, N) / span) * a.w;
      const columns = [];
      for (let x = a.x; x < a.x + a.w; x += 2) {
        const f = first + ((x - a.x) / a.w) * span;
        columns.push([x, ov[Math.floor((mod(f, N) / N) * ov.length)] / 127]);
      }
      wave(columns, mid, half);
      for (let i = 1; i < heads.length; i++) {
        const v = heads[i];
        const x0 = xOf(v.start);
        const x1 = Math.min(a.x + a.w, x0 + (v.length / span) * a.w);
        const hh = Math.max(4, v.amp * half);
        frameRect(x0, mid - hh, Math.max(3, x1 - x0), hh * 2, a.accent, 0.9);
        const px = Math.min(a.x + a.w - 1, xOf(v.start + mod(v.pos - v.start, Math.max(1, v.length))));
        rect(px - 1, mid - hh - 2, 3, 2, a.accent);
        rect(px, mid - hh, 1, hh * 2, a.accent);
      }
      if (listening) rect(a.x + a.w - 1, top, 1, bottom - top, a.accent);
      const hz = S.sampleRate * loop.rate;
      const secs = hz > 0 ? span / hz : 0;
      text(a.x + a.w, a.y + 24, (listening ? "Listening " : "Playing ") + secs.toFixed(secs < 10 ? 1 : 0) + " s", 8.5, DIM, false, "right");
    } else hdots(a.x, a.x + a.w, mid);
    // Env input against the threshold, -60..0 dB.
    const ey = a.y + a.h - 28;
    text(a.x, ey + 7, "Env", 8.5, DIM, false);
    const ex = a.x + 22;
    const ew = a.w - 22;
    rect(ex, ey + 2, ew, 5, FAINT);
    const inDb = last("Env input");
    const thDb = last("Env threshold");
    if (inDb !== null) rect(ex, ey + 2, ((inDb + 60) / 60) * ew, 5, INK, 0.85);
    if (thDb !== null) rect(ex + ((thDb + 60) / 60) * ew, ey, 1, 9, a.accent);
    chips(a.x, a.y + a.h - 12, [["Wet", on("Wet on")], ["Loop", on("Looper")], ["Freeze", on("Freeze")], ["Dub", on("Overdub")]], a.accent, a.x + a.w);
  },

  // The mode and X's job in it, the decay readout, the tail it makes, and
  // the LFO moving under it.
  slowdrift(a) {
    const mode = choice("Mode", "—");
    text(a.x, a.y + 11, mode, 13, INK, true);
    const X = { Dream: "X: vibrato on the reverb", Light: "X: octave-up shimmer", Rain: "X: echoes to wash", Dark: "X: an octave down", Rise: "X: swell time" };
    text(a.x, a.y + 24, X[mode] || "", 8.5, DIM, false);
    const frozen = on("Freeze");
    const decay = last("Decay");
    text(a.x + a.w, a.y + 9, frozen ? "∞" : decay !== null ? decay.toFixed(1) + " s" : "", frozen ? 12 : 9, INK, true, "right");
    // The tail: 60 dB down over the decay, on a time axis twice as long.
    const top = a.y + 32;
    const bottom = a.y + a.h - 36;
    const span = Math.max(2, (decay || 4) * 2);
    const pts = [[a.x, bottom]];
    for (let i = 0; i <= 60; i++) {
      const t = (i / 60) * span;
      const g = frozen ? 0.8 : Math.pow(10, (-3 * t) / (decay || 4));
      pts.push([a.x + (i / 60) * a.w, bottom - g * (bottom - top)]);
    }
    pts.push([a.x + a.w, bottom]);
    colour(a.accent, 0.22);
    mgraphics.move_to(pts[0][0], pts[0][1]);
    for (let i = 1; i < pts.length; i++) mgraphics.line_to(pts[i][0], pts[i][1]);
    mgraphics.close_path();
    mgraphics.fill();
    line(pts.slice(1, -1), a.accent, 1, 1.5);
    hdots(a.x, a.x + a.w, bottom);
    // The LFO, with its shape.
    const ly = a.y + a.h - 30;
    text(a.x, ly + 9, choice("Waveshape", "LFO"), 8.5, DIM, false);
    line(tracePoints(trace("LFO"), a.x + 44, ly, a.w - 44, 12), INK, 0.9, 1);
    chips(a.x, a.y + a.h - 12, [[choice("Width", "Width"), true], ["Sustain", on("Sustain")], ["Freeze", frozen]], a.accent, a.x + a.w);
  },

  // The sea: its swell by the sea's level and Tide, rougher with the
  // storm; clouds, rain and wind by theirs; lightning flashes the sky.
  stormsea(a) {
    const frozen = on("Freeze");
    const decay = last("Decay");
    const storm = (last("Storm") || 0) / 100;
    const rain = (last("Rain") || 0) / 100;
    const wind = (last("Wind") || 0) / 100;
    const flash = (last("Flash") || 0) / 100;
    const sea = last("Sea");
    const tide = last("Tide") || 0;
    const top = a.y;
    const bottom = a.y + a.h - 16;
    if (flash > 0.05) rect(a.x - 6, top - 6, a.w + 12, bottom - top + 6, INK, flash * 0.5);
    text(a.x, a.y + 11, "Storm " + Math.round(storm * 100) + "%", 11, INK, true);
    text(a.x + a.w, a.y + 10, frozen || (decay !== null && decay >= 999) ? "∞" : decay !== null ? decay.toFixed(1) + " s" : "", frozen ? 12 : 9, INK, true, "right");
    const t = Date.now() / 1000;
    // Clouds: lumps along the top, more of them in a storm.
    const clouds = 2 + Math.round(storm * 5);
    for (let i = 0; i < clouds; i++) {
      const cx = a.x + ((i * 0.37 + t * 0.01 * (1 + wind)) % 1) * a.w;
      const r = 5 + ((i * 7) % 4);
      colour(DIM, 0.55 + storm * 0.3);
      mgraphics.ellipse(cx - r * 1.8, a.y + 18, r * 3.6, r * 1.6);
      mgraphics.fill();
    }
    // Rain: slanted streaks, slanting and blowing more in the wind, each
    // drop at its own place and speed.
    const drops = Math.round(rain * 40);
    for (let i = 0; i < drops; i++) {
      const x = a.x + wrap(hash(i) - t * 0.05 * wind) * a.w;
      const y = a.y + 30 + wrap(hash(i + 97) + t * (0.7 + 0.4 * hash(i + 31))) * (bottom - a.y - 60);
      line([[x, y], [x - 2 - wind * 3, y + 5]], INK, 0.55, 1);
    }
    if (flash > 0.3) {
      const bx = a.x + a.w * 0.62;
      line([[bx, a.y + 24], [bx - 5, a.y + 40], [bx + 2, a.y + 42], [bx - 4, a.y + 60]], INK, 1, 1.5);
    }
    // The sea: three rows of swell.
    const level = sea === null ? 0.5 : Math.max(0, Math.min(1, (sea + 60) / 60));
    const base = bottom - 10 - tide * 2;
    for (let row = 0; row < 3; row++) {
      const amp = frozen ? 0.6 : (1 + storm * 5) * level * (1 - row * 0.2);
      const pts = [];
      for (let x = 0; x <= a.w; x += 2) pts.push([a.x + x, base + row * 5 - amp * Math.abs(Math.sin(x / (9 + row * 4) + t * (0.8 + storm) + row))]);
      line(pts, row ? DIM : a.accent, 1, row ? 1 : 1.5);
    }
    chips(a.x, a.y + a.h - 12, [["Calm", on("Calm")], ["Squall", on("Squall")], ["Freeze", frozen]], a.accent, a.x + a.w);
  },

  // The edited layer and the layer mode, its three envelopes, and the last
  // voice's cutoff and level as they move.
  wavebyte(a) {
    const layer = choice("Layer", "A");
    rect(a.x, a.y, 16, 14, a.accent);
    text(a.x + 8, a.y + 11, layer, 11, BG, true, "center");
    fitText(a.x + 22, a.y + 11, choice("Layer Mode", ""), 10, INK, true, a.w - 22);
    text(a.x, a.y + 26, "Tables " + choice("Table 1", "—") + " · " + choice("Table 2", "—"), 8.5, DIM, false);
    // Envelopes: attack, decay to sustain, a hold, release.
    const top = a.y + 34;
    const eh = 30;
    const ew = (a.w - 8) / 3;
    ["Wave", "Filter", "Amp"].forEach((part, i) => {
      const v = (n) => (value(part + " " + n) ? value(part + " " + n).v : 0.5);
      const x = a.x + i * (ew + 4);
      const total = 0.15 + v("Attack") + v("Decay") + v("Release");
      const sx = (ew - 2) / (total + 0.35);
      const sus = top + eh - v("Sustain") * eh;
      const xa = x + v("Attack") * sx;
      const xd = xa + v("Decay") * sx;
      const xh = xd + 0.35 * sx;
      const xr = xh + v("Release") * sx;
      line([[x, top + eh], [xa, top], [xd, sus], [xh, sus], [xr, top + eh]], i === 2 ? a.accent : INK, 1, 1.2);
      hdots(x, x + ew - 2, top + eh);
      text(x, top + eh + 10, part, 8, DIM, false);
    });
    // Cutoff and level, last voice.
    const ty = top + eh + 14;
    const th = a.y + a.h - 16 - ty;
    text(a.x, ty + 8, "Cutoff", 8, DIM, false);
    text(a.x + 34, ty + 8, "Level", 8, a.accent, false);
    line(fitted(trace("Cutoff"), a.x, ty + 10, a.w, th - 10), DIM, 1, 1);
    line(fitted(trace("Amp"), a.x, ty + 10, a.w, th - 10), a.accent, 1, 1.2);
    const voices = last("Voices");
    chips(a.x, a.y + a.h - 12, [["FX " + choice("FX 1 Type", "Off"), choice("FX 1 Type", "Off") !== "Off"], ["FX " + choice("FX 2 Type", "Off"), choice("FX 2 Type", "Off") !== "Off"], [(voices === null ? 0 : Math.round(voices)) + " voices", false]], a.accent, a.x + a.w);
  },
};

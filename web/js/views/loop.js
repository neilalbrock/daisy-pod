// The loop view, for an always-listening looper (driftnet): while it
// listens, the ring it records into scrolls right to left (now is the right
// edge); while it plays, the loop is laid out left to right, oldest first,
// with what's playing it on top: a window, a slice, or grains, each with its
// play head. It takes the same frames as the buffer view; the first head
// (marked as a tap) is the loop itself, with the clock as its rate.

import { rateHue } from "./buffer.js";

const MARGIN_TOP = 16;
const MARGIN_BOTTOM = 18;
const MAX_EXTRAPOLATE_MS = 120; // keep moving between frames, but not far

// Colours come from the canvas, so the page can theme the panel it sits in.
function css(el, name) {
  return getComputedStyle(el).getPropertyValue(name).trim();
}

const mod = (x, n) => ((x % n) + n) % n;

export class LoopView {
  constructor(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext("2d");
    this.spanSeconds = 8; // the page's span buttons set it; the loop is always shown whole
    this.layout = null;
    this.frame = null;
    this.frameAt = 0;
    this.colours = {};
    new ResizeObserver(() => this.resize()).observe(canvas);
    this.resize();
  }

  resize() {
    const dpr = window.devicePixelRatio || 1;
    const { width, height } = this.canvas.getBoundingClientRect();
    this.canvas.width = Math.max(1, Math.round(width * dpr));
    this.canvas.height = Math.max(1, Math.round(height * dpr));
    this.dpr = dpr;
    this.colours = {
      wave: css(this.canvas, "--wave"),
      text: css(this.canvas, "--text-dim"),
      tap: css(this.canvas, "--tap"),
      now: css(this.canvas, "--accent-ui"),
    };
  }

  setLayout(hello) {
    this.layout = {
      sampleRate: hello.sampleRate,
      frames: hello.bufferFrames,
      columns: hello.overviewColumns,
      columnFrames: hello.bufferFrames / hello.overviewColumns,
    };
    this.overview = new Uint8Array(hello.overviewColumns);
    this.frame = null;
  }

  addOverview({ first, values }) {
    if (!this.overview) return;
    for (let i = 0; i < values.length; i++) this.overview[(first + i) % this.overview.length] = values[i];
  }

  setFrame(frame, at) {
    if (frame.buffer && frame.buffer.heads.length > 0) {
      this.frame = frame.buffer;
      this.frameAt = at;
    }
  }

  draw(now) {
    const { ctx, canvas, dpr } = this;
    const w = canvas.width / dpr;
    const h = canvas.height / dpr;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, w, h);
    const L = this.layout;
    if (!L || !this.frame) {
      ctx.fillStyle = this.colours.text;
      ctx.font = "12px ui-monospace, monospace";
      ctx.textAlign = "center";
      ctx.fillText(L ? "Waiting for the Pod…" : "Not connected", w / 2, h / 2);
      this.statusText = "";
      return;
    }

    const N = L.frames;
    const [loop, ...heads] = this.frame.heads;
    // Frames of the ring per ms, at the clock the core runs at.
    const perMs = (L.sampleRate * loop.rate) / 1000;
    const ahead = Math.max(0, Math.min(now - this.frameAt, MAX_EXTRAPOLATE_MS)) * perMs;
    const top = MARGIN_TOP;
    const bottom = h - MARGIN_BOTTOM;
    const plotH = bottom - top;
    const mid = top + plotH / 2;

    let first, span, label;
    if (loop.length === 0) {
      // Listening: the whole ring, newest at the right.
      span = N;
      first = this.frame.write + ahead - N;
      label = "Listening";
    } else {
      span = loop.length;
      first = loop.start;
      label = "Playing";
    }
    const xOf = (frame) => (mod(frame - first, N) / span) * w;

    this.drawWave(w, mid, plotH, first, span, N);

    // What plays the loop: boxes over the part each one loops, with their
    // play heads. Grains sit one above the other, kept inside the plot.
    const pad = 10;
    for (const v of heads) {
      const bh = heads.length > 1 ? plotH * 0.3 : plotH - 2 * pad;
      const y = heads.length > 1 ? top + pad + bh / 2 + v.pan * (plotH - 2 * pad - bh) : mid;
      const x0 = xOf(v.start);
      const x1 = x0 + (v.length / span) * w;
      const hue = rateHue(v.rate);
      ctx.globalAlpha = 0.12 + 0.3 * v.amp;
      ctx.fillStyle = `hsl(${hue} 85% 60%)`;
      ctx.beginPath();
      ctx.roundRect(Math.max(0, x0), y - bh / 2, Math.max(2, Math.min(w, x1) - Math.max(0, x0)), bh, 3);
      ctx.fill();

      // The play head moves on between frames, round its part of the loop.
      const into = mod(v.pos - v.start + (v.reverse ? -1 : 1) * v.rate * ahead, Math.max(1, v.length));
      const x = xOf(v.start + into);
      ctx.globalAlpha = 0.5 + 0.5 * Math.min(1, v.amp * 2);
      ctx.fillStyle = `hsl(${hue} 95% 70%)`;
      ctx.fillRect(x - 1, y - bh / 2 - 3, 2, bh + 6);
      const dir = v.reverse ? -1 : 1;
      ctx.beginPath();
      ctx.moveTo(x + dir * 6, y);
      ctx.lineTo(x + dir * 1, y - 4);
      ctx.lineTo(x + dir * 1, y + 4);
      ctx.closePath();
      ctx.fill();
    }
    ctx.globalAlpha = 1;

    // Now: the write head while listening, the right edge.
    if (loop.length === 0) {
      ctx.fillStyle = this.colours.now;
      ctx.fillRect(w - 2, top, 2, plotH);
    }

    // The loop's length in seconds at the current clock goes in the panel's
    // head (status()); the ends are labelled here.
    const secs = span / (L.sampleRate * loop.rate);
    this.statusText = `${label} · ${secs.toFixed(secs < 10 ? 2 : 1)} s at ${Math.round((L.sampleRate * loop.rate) / 1000)}k`;
    ctx.fillStyle = this.colours.text;
    ctx.font = "10px ui-monospace, monospace";
    ctx.textAlign = "left";
    ctx.fillText("oldest", 4, bottom + 13);
    ctx.textAlign = "right";
    ctx.fillText(loop.length === 0 ? "now" : "newest", w - 4, bottom + 13);
  }

  // What the view is showing, for the panel's head.
  status() {
    return this.statusText || "";
  }

  drawWave(w, mid, plotH, first, span, N) {
    const { ctx } = this;
    const L = this.layout;
    const cols = this.overview;
    const half = plotH / 2 - 4;
    ctx.fillStyle = this.colours.wave;
    for (let x = 0; x < w; x++) {
      const f0 = first + (x / w) * span;
      const f1 = first + ((x + 1) / w) * span;
      const c0 = Math.floor(mod(f0, N) / L.columnFrames);
      const c1 = Math.floor(mod(f1, N) / L.columnFrames);
      let v = 0;
      for (let c = c0; ; c = (c + 1) % cols.length) {
        if (cols[c] > v) v = cols[c];
        if (c === c1) break;
      }
      if (v === 0) continue;
      const amp = (v / 127) * half;
      ctx.fillRect(x, mid - amp, 1, amp * 2);
    }
  }
}

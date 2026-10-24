// The buffer view: the last few seconds of the sample buffer scrolling right
// to left (now is the right edge), with the beat grid, note onsets, every
// voice's slice and play head, and delay taps. Voices sit at their stereo
// position (left at the top) and are coloured by playback speed.

const MARGIN_TOP = 16;
const MARGIN_BOTTOM = 18;
const MAX_EXTRAPOLATE_MS = 120; // keep scrolling between frames, but not far

function css(name) {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}

// Hue for a playback speed: teal at normal, orange to red going up, blue to
// violet going down.
export function rateHue(rate) {
  const l = Math.max(-2, Math.min(2, Math.log2(Math.max(rate, 0.01))));
  return l >= 0 ? 165 - 130 * Math.min(l, 1) - 25 * Math.max(l - 1, 0) : 165 + 55 * Math.min(-l, 1) + 45 * Math.max(-l - 1, 0);
}

export class BufferView {
  constructor(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext("2d");
    this.spanSeconds = 8;
    this.layout = null;
    this.frame = null;
    this.frameAt = 0;
    this.tempo = null;
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
      wave: css("--wave"),
      grid: css("--grid"),
      gridStrong: css("--grid-strong"),
      text: css("--text-dim"),
      onset: css("--onset"),
      tap: css("--tap"),
      now: css("--accent-ui"),
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
    if (frame.buffer) {
      this.frame = frame.buffer;
      this.frameAt = at;
    }
    if (frame.tempo) this.tempo = frame.tempo;
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
      ctx.font = "13px system-ui, sans-serif";
      ctx.textAlign = "center";
      ctx.fillText(L ? "Waiting for the Pod…" : "Not connected", w / 2, h / 2);
      return;
    }

    const sr = L.sampleRate;
    const N = L.frames;
    const span = Math.min(this.spanSeconds * sr, N * 0.999);
    const ahead = (Math.max(0, Math.min(now - this.frameAt, MAX_EXTRAPOLATE_MS)) * sr) / 1000;
    const write = this.frame.write + ahead;
    const top = MARGIN_TOP;
    const bottom = h - MARGIN_BOTTOM;
    const plotH = bottom - top;
    const mid = top + plotH / 2;
    const ageOf = (frame) => (((write - frame) % N) + N) % N;
    const xOf = (age) => w - (age / span) * w;

    this.drawWave(w, mid, plotH, span, write, N);
    this.drawGrid(w, top, bottom, span, sr, now);

    // Onsets
    ctx.fillStyle = this.colours.onset;
    ctx.strokeStyle = this.colours.onset;
    ctx.globalAlpha = 0.35;
    for (const o of this.frame.onsets) {
      const age = ageOf(o);
      if (age > span) continue;
      const x = xOf(age);
      ctx.beginPath();
      ctx.moveTo(x, top);
      ctx.lineTo(x, bottom);
      ctx.lineWidth = 1;
      ctx.stroke();
    }
    ctx.globalAlpha = 1;
    for (const o of this.frame.onsets) {
      const age = ageOf(o);
      if (age > span) continue;
      const x = xOf(age);
      ctx.beginPath();
      ctx.moveTo(x - 4, 2);
      ctx.lineTo(x + 4, 2);
      ctx.lineTo(x, top - 4);
      ctx.closePath();
      ctx.fill();
    }

    // Voices and taps
    const pad = 10;
    const yOf = (pan) => top + pad + pan * (plotH - 2 * pad);
    for (const v of this.frame.heads) {
      // Move each head on between frames too: a tap keeps pace with the
      // write head; a voice reads at its rate, round its slice.
      let pos = v.pos + ahead;
      if (!v.tap && v.length > 0) {
        const into = ((((v.pos - v.start) % N) + N) % N) + (v.reverse ? -1 : 1) * v.rate * ahead;
        pos = v.start + (((into % v.length) + v.length) % v.length);
      }
      const posAge = ageOf(pos);
      if (v.tap) {
        if (posAge > span) continue;
        const x = xOf(posAge);
        ctx.globalAlpha = 0.25 + 0.75 * v.amp;
        ctx.strokeStyle = this.colours.tap;
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.moveTo(x, top);
        ctx.lineTo(x, bottom);
        ctx.stroke();
        // The dot shows the tap's speed when it's pitch-shifted.
        ctx.fillStyle = Math.abs(v.rate - 1) > 0.01 ? `hsl(${rateHue(v.rate)} 90% 65%)` : this.colours.tap;
        ctx.beginPath();
        ctx.arc(x, yOf(v.pan), 4, 0, Math.PI * 2);
        ctx.fill();
        continue;
      }
      const startAge = ageOf(v.start);
      const endAge = startAge - v.length;
      if (endAge > span || startAge < 0) continue;
      const x0 = xOf(Math.min(startAge, span));
      const x1 = xOf(Math.max(endAge, 0));
      const y = yOf(v.pan);
      const bh = 5 + 15 * Math.sqrt(v.amp);
      const hue = rateHue(v.rate);
      ctx.globalAlpha = 0.18 + 0.4 * v.amp;
      ctx.fillStyle = `hsl(${hue} 85% 60%)`;
      ctx.beginPath();
      ctx.roundRect(x0, y - bh / 2, Math.max(2, x1 - x0), bh, 3);
      ctx.fill();

      // Play head, pointing the way it's reading
      if (posAge <= span) {
        const x = xOf(posAge);
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
    }
    ctx.globalAlpha = 1;

    // Now
    ctx.fillStyle = this.colours.now;
    ctx.fillRect(w - 2, top, 2, plotH);
  }

  drawGrid(w, top, bottom, span, sr, now) {
    const { ctx } = this;
    const t = this.tempo;
    ctx.lineWidth = 1;
    ctx.globalAlpha = 0.8;
    if (t && t.bpm > 0) {
      const spb = (sr * 60) / t.bpm;
      const elapsed = Math.max(0, Math.min(now - this.frameAt, MAX_EXTRAPOLATE_MS));
      const beat = t.beat + (elapsed * t.bpm) / 60000;
      const frac = beat - Math.floor(beat);
      const steps = t.stepsPerBeat >= 1 ? t.stepsPerBeat : 1;
      const stepPx = (spb / steps / span) * w;
      for (let k = 0; ; k++) {
        const age = (frac + k) * spb;
        if (age > span) break;
        const x = Math.round(w - (age / span) * w) + 0.5;
        const index = Math.floor(beat) - k;
        ctx.strokeStyle = index % 4 === 0 ? this.colours.gridStrong : this.colours.grid;
        ctx.beginPath();
        ctx.moveTo(x, top);
        ctx.lineTo(x, bottom);
        ctx.stroke();
        if (steps > 1 && stepPx > 6) {
          ctx.globalAlpha = 0.35;
          ctx.strokeStyle = this.colours.grid;
          for (let s = 1; s < steps; s++) {
            const xs = Math.round(x - s * stepPx) + 0.5;
            ctx.beginPath();
            ctx.moveTo(xs, top + (bottom - top) * 0.35);
            ctx.lineTo(xs, bottom - (bottom - top) * 0.35);
            ctx.stroke();
          }
          ctx.globalAlpha = 1;
        }
      }
    }

    ctx.globalAlpha = 1;

    // Seconds ago
    // Labels at least 56 px apart.
    const secs = span / sr;
    const every = [0.25, 0.5, 1, 2, 4, 8].find((e) => (e / secs) * w >= 56) || 8;
    ctx.fillStyle = this.colours.text;
    ctx.font = "11px system-ui, sans-serif";
    ctx.textAlign = "center";
    for (let s = every; s < secs; s += every) {
      const x = w - (s / secs) * w;
      ctx.fillText(`−${s % 1 ? s.toFixed(2) : s} s`, x, bottom + 13);
    }
    ctx.textAlign = "right";
    ctx.fillText("now", w - 4, bottom + 13);
  }

  drawWave(w, mid, plotH, span, write, N) {
    const { ctx } = this;
    const L = this.layout;
    const cols = this.overview;
    const half = plotH / 2 - 4;
    ctx.fillStyle = this.colours.wave;
    for (let x = 0; x < w; x++) {
      const ageHi = ((w - x) / w) * span;
      const ageLo = ((w - x - 1) / w) * span;
      const c0 = Math.floor((((write - ageHi) % N) + N) % N / L.columnFrames);
      const c1 = Math.floor((((write - ageLo) % N) + N) % N / L.columnFrames);
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

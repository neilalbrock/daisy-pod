// The display's trace strip and X-Y plot, for apps that send them. The
// strip overlays the traces the app picks (extras.displayTraces), each
// scaled to its own range, over the last few seconds; the rest show as
// numbers (readouts.js).

const SPAN_MS = 8000;

// Colours come from the canvas, so the page can theme the panel it sits in.
function css(el, name) {
  return getComputedStyle(el).getPropertyValue(name).trim();
}

function setupCanvas(canvas, onResize) {
  const fit = () => {
    const dpr = window.devicePixelRatio || 1;
    const { width, height } = canvas.getBoundingClientRect();
    canvas.width = Math.max(1, Math.round(width * dpr));
    canvas.height = Math.max(1, Math.round(height * dpr));
    onResize(dpr);
  };
  new ResizeObserver(fit).observe(canvas);
  fit();
}

export class TraceStrip {
  constructor(canvas, legend) {
    this.canvas = canvas;
    this.legend = legend; // where the plotted traces' names and latest values go
    this.ctx = canvas.getContext("2d");
    this.traces = [];
    this.shown = []; // indices of the traces plotted
    this.points = []; // {t, v: [raw 0..1 per trace]}
    this.period = 1;
    setupCanvas(canvas, (dpr) => {
      this.dpr = dpr;
      this.colours = {
        grid: css(canvas, "--grid-strong"),
        series: [1, 2, 3, 4].map((i) => css(canvas, `--series-${i}`)),
      };
    });
  }

  setLayout(hello, names) {
    this.traces = hello.traces;
    this.period = hello.tracePeriodMs || 1;
    this.points = [];
    this.shown = hello.traces.map((t, i) => (names.includes(t.name) ? i : -1)).filter((i) => i >= 0);
    this.legend.replaceChildren();
    this.legendValues = this.shown.map((ti, k) => {
      const span = document.createElement("span");
      span.style.color = this.colours?.series[k % 4] ?? "";
      span.textContent = `${this.traces[ti].name} –`;
      this.legend.append(span, " ");
      return { el: span, ti, text: "" };
    });
  }

  add(points, at) {
    const n = points.length;
    points.forEach((v, i) => this.points.push({ t: at - (n - 1 - i) * this.period, v }));
    const keep = at - SPAN_MS - 1000;
    while (this.points.length && this.points[0].t < keep) this.points.shift();
  }

  value(ti, raw) {
    const t = this.traces[ti];
    return t.min + raw * (t.max - t.min);
  }

  draw(now) {
    const { ctx, dpr } = this;
    const w = this.canvas.width / dpr;
    const h = this.canvas.height / dpr;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, w, h);
    if (!this.shown.length) return;
    const last = this.points[this.points.length - 1];
    const end = last ? Math.min(now, last.t + 80) : now;
    const xOf = (t) => ((t - (end - SPAN_MS)) / SPAN_MS) * w;
    const pad = 3;

    // A centre line when a plotted trace runs either side of zero.
    if (this.shown.some((ti) => this.traces[ti].min < 0 && this.traces[ti].max > 0)) {
      ctx.strokeStyle = this.colours.grid;
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(0, Math.round(h / 2) + 0.5);
      ctx.lineTo(w, Math.round(h / 2) + 0.5);
      ctx.stroke();
    }

    // Each trace as a min-max envelope per pixel, so spikes survive.
    this.shown.forEach((ti, k) => {
      const yOf = (raw) => pad + (1 - raw) * (h - 2 * pad);
      ctx.fillStyle = this.colours.series[k % 4];
      let px = -1;
      let lo = 0;
      let hi = 0;
      let prev = null;
      const flush = () => {
        if (px < 0) return;
        const y0 = yOf(hi);
        const y1 = yOf(lo);
        ctx.fillRect(px, y0 - 0.75, 1, Math.max(1.5, y1 - y0 + 1.5));
      };
      for (const p of this.points) {
        if (p.t < end - SPAN_MS - 50) continue;
        const v = p.v[ti];
        const x = Math.floor(xOf(p.t));
        if (x !== px) {
          flush();
          px = x;
          lo = hi = v;
          if (prev !== null) {
            lo = Math.min(lo, prev);
            hi = Math.max(hi, prev);
          }
        } else {
          lo = Math.min(lo, v);
          hi = Math.max(hi, v);
        }
        prev = v;
      }
      flush();
    });

    // The latest values, in the display's head.
    if (last)
      for (const item of this.legendValues) {
        const t = this.traces[item.ti];
        const v = this.value(item.ti, last.v[item.ti]);
        const text = `${t.name} ${v.toFixed(t.max - t.min > 50 ? 0 : 1).replace("-", "−")}`;
        if (text !== item.text) item.el.textContent = item.text = text;
      }
  }
}

// An X-Y plot of the latest few frames, older ones fading, like a scope.
export class XYView {
  constructor(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext("2d");
    this.frames = [];
    setupCanvas(canvas, (dpr) => {
      this.dpr = dpr;
      this.colours = {
        line: css(canvas, "--accent-ui"),
        grid: css(canvas, "--grid"),
        gridStrong: css(canvas, "--grid-strong"),
        text: css(canvas, "--text-dim"),
      };
      this.dirty = true;
    });
  }

  add(points) {
    this.frames.push(points);
    if (this.frames.length > 6) this.frames.shift();
    this.dirty = true;
  }

  clear() {
    this.frames = [];
    this.dirty = true;
  }

  draw() {
    if (!this.dirty) return;
    this.dirty = false;
    const { ctx, dpr } = this;
    const w = this.canvas.width / dpr;
    const h = this.canvas.height / dpr;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, w, h);
    const size = Math.min(w, h) - 4;
    const cx = w / 2;
    const cy = h / 2;
    const x = (v) => cx + (v * size) / 2;
    const y = (v) => cy - (v * size) / 2;

    ctx.strokeStyle = this.colours.grid;
    ctx.lineWidth = 1;
    ctx.strokeRect(cx - size / 2 + 0.5, cy - size / 2 + 0.5, size - 1, size - 1);
    ctx.beginPath();
    ctx.moveTo(cx + 0.5, cy - size / 2);
    ctx.lineTo(cx + 0.5, cy + size / 2);
    ctx.moveTo(cx - size / 2, cy + 0.5);
    ctx.lineTo(cx + size / 2, cy + 0.5);
    ctx.stroke();
    // A straight line would mean no saturation.
    ctx.setLineDash([3, 4]);
    ctx.strokeStyle = this.colours.gridStrong;
    ctx.beginPath();
    ctx.moveTo(x(-1), y(-1));
    ctx.lineTo(x(1), y(1));
    ctx.stroke();
    ctx.setLineDash([]);

    ctx.lineWidth = 1.5;
    ctx.lineJoin = "round";
    this.frames.forEach((pts, fi) => {
      ctx.globalAlpha = 0.15 + (0.85 * (fi + 1)) / this.frames.length;
      ctx.strokeStyle = this.colours.line;
      ctx.beginPath();
      pts.forEach(([px, py], i) => (i ? ctx.lineTo(x(px), y(py)) : ctx.moveTo(x(px), y(py))));
      ctx.stroke();
    });
    ctx.globalAlpha = 1;

    ctx.fillStyle = this.colours.text;
    ctx.font = "9px ui-monospace, monospace";
    ctx.textAlign = "right";
    ctx.fillText("in →", cx + size / 2 - 3, cy + size / 2 - 4);
    ctx.textAlign = "left";
    ctx.fillText("out ↑", cx - size / 2 + 4, cy - size / 2 + 11);
  }
}

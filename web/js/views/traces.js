// Trace and X-Y views, for apps that send them (tapeworn: tape speed and
// level, and the saturator's loop).

const LANE_GAP = 10;

function css(name) {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
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

// Values over time, scrolling right to left. Traces with the same unit and
// range share a lane (left and right channels together).
export class TraceView {
  constructor(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext("2d");
    this.spanSeconds = 8;
    this.lanes = [];
    this.points = []; // {t, v: [raw 0..1 per trace]}
    setupCanvas(canvas, (dpr) => {
      this.dpr = dpr;
      this.colours = {
        text: css("--text-dim"),
        grid: css("--grid"),
        gridStrong: css("--grid-strong"),
        series: [css("--accent-ui"), css("--tap"), css("--onset"), css("--text")],
      };
    });
  }

  setLayout(hello) {
    this.traces = hello.traces;
    this.period = hello.tracePeriodMs || 1;
    this.points = [];
    const lanes = new Map();
    hello.traces.forEach((t, i) => {
      const key = `${t.unit}|${t.min}|${t.max}`;
      if (!lanes.has(key)) lanes.set(key, { unit: t.unit, min: t.min, max: t.max, members: [] });
      lanes.get(key).members.push(i);
    });
    this.lanes = [...lanes.values()].map((lane) => {
      // "Speed L" and "Speed R" make a lane called "Speed".
      const names = lane.members.map((i) => hello.traces[i].name);
      const words = names[0].split(" ");
      const common = words.filter((w, j) => names.every((n) => n.split(" ")[j] === w)).join(" ");
      return { ...lane, title: common || names.join(", ") };
    });
  }

  add(points, at) {
    const n = points.length;
    points.forEach((v, i) => this.points.push({ t: at - (n - 1 - i) * this.period, v }));
    const keep = at - 30000;
    while (this.points.length && this.points[0].t < keep) this.points.shift();
  }

  value(i, raw) {
    const t = this.traces[i];
    return t.min + raw * (t.max - t.min);
  }

  draw(now) {
    const { ctx, dpr } = this;
    const w = this.canvas.width / dpr;
    const h = this.canvas.height / dpr;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, w, h);
    if (!this.lanes.length) return;

    const span = this.spanSeconds * 1000;
    const laneH = (h - LANE_GAP * (this.lanes.length - 1) - 14) / this.lanes.length;
    const last = this.points[this.points.length - 1];
    // Scroll on smoothly between frames, but not past the newest point by much.
    const end = last ? Math.min(now, last.t + 80) : now;
    const xOf = (t) => ((t - (end - span)) / span) * w;

    this.lanes.forEach((lane, li) => {
      const top = li * (laneH + LANE_GAP);
      // Room for the labels above the plot.
      const plotTop = top + 18;
      const plotH = laneH - 22;
      const yOf = (v) => plotTop + plotH - ((v - lane.min) / (lane.max - lane.min)) * plotH;

      ctx.fillStyle = "rgb(255 255 255 / 0.02)";
      ctx.fillRect(0, top, w, laneH);
      ctx.strokeStyle = this.colours.grid;
      ctx.lineWidth = 1;
      if (lane.min < 0 && lane.max > 0) {
        ctx.strokeStyle = this.colours.gridStrong;
        ctx.beginPath();
        ctx.moveTo(0, Math.round(yOf(0)) + 0.5);
        ctx.lineTo(w, Math.round(yOf(0)) + 0.5);
        ctx.stroke();
      }

      // Each trace as a min-max envelope per pixel, so spikes survive.
      lane.members.forEach((ti, mi) => {
        ctx.fillStyle = this.colours.series[mi % this.colours.series.length];
        let px = -1;
        let lo = 0;
        let hi = 0;
        let prev = null;
        const flush = () => {
          if (px < 0) return;
          const y0 = yOf(Math.min(hi, lane.max));
          const y1 = yOf(Math.max(lo, lane.min));
          ctx.fillRect(px, y0 - 0.75, 1, Math.max(1.5, y1 - y0 + 1.5));
        };
        for (const p of this.points) {
          if (p.t < end - span - 50) continue;
          const v = this.value(ti, p.v[ti]);
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

      // Labels: the lane's name and range, and each trace's latest value.
      ctx.font = "11px system-ui, sans-serif";
      ctx.textAlign = "left";
      ctx.fillStyle = this.colours.text;
      ctx.fillText(`${lane.title} (${lane.unit})`, 6, top + 13);
      ctx.fillText(`${lane.max}`, 6, plotTop + 11);
      ctx.fillText(`${lane.min}`, 6, plotTop + plotH - 3);
      if (last) {
        ctx.textAlign = "right";
        let x = w - 8;
        for (let mi = lane.members.length - 1; mi >= 0; mi--) {
          const ti = lane.members[mi];
          const v = this.value(ti, last.v[ti]);
          const text = `${this.traces[ti].name.split(" ").slice(-1)[0]} ${v.toFixed(lane.max - lane.min > 50 ? 0 : 1)}`;
          ctx.fillStyle = this.colours.series[mi % this.colours.series.length];
          ctx.fillText(text, x, top + 13);
          x -= ctx.measureText(text).width + 12;
        }
      }
    });

    // Seconds ago
    ctx.fillStyle = this.colours.text;
    ctx.textAlign = "center";
    const secs = this.spanSeconds;
    const every = [0.25, 0.5, 1, 2, 4].find((e) => (e / secs) * w >= 56) || 4;
    for (let s = every; s < secs; s += every) ctx.fillText(`−${s} s`, w - (s / secs) * w, h - 2);
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
      this.colours = { line: css("--accent-ui"), grid: css("--grid"), gridStrong: css("--grid-strong"), text: css("--text-dim") };
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
    const size = Math.min(w, h) - 8;
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
    ctx.font = "11px system-ui, sans-serif";
    ctx.textAlign = "right";
    ctx.fillText("in →", cx + size / 2 - 4, cy + size / 2 - 5);
    ctx.textAlign = "left";
    ctx.fillText("out ↑", cx - size / 2 + 5, cy - size / 2 + 13);
  }
}

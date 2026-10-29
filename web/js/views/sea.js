// The sea view, for stormsea: the sea drawn from its traces. The surface
// rises and falls with the tail's level and Tide, and chops with the wind;
// clouds gather as the storm rises; rain falls; each lightning strike
// draws a bolt and lights the sky (not with reduced motion); thunder shakes
// the water. Along the top, the storm's strength over the last minute.
//
// It draws from the traces, not a buffer (fromTraces), so the page shows
// it in the display for an app that sends no buffer.

const MINUTE_MS = 60000;
const HISTORY_EVERY_MS = 250;
const BOLT_MS = 260;
const MAX_DROPS = 160;

// Colours come from the canvas, so the page can theme the panel it sits in.
function css(el, name) {
  return getComputedStyle(el).getPropertyValue(name).trim();
}

const NAMES = ["Sea", "Tide", "Storm", "Flash", "Wind", "Rain", "Thunder"];

export class SeaView {
  static fromTraces = true;

  constructor(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext("2d");
    this.index = {};
    this.now = null; // the latest values, in units
    this.shown = null; // eased towards them, frame by frame
    this.history = []; // {t, storm}
    this.bolts = [];
    this.drops = [];
    this.lastDraw = 0;
    this.lastFlash = 0;
    this.motion = window.matchMedia?.("(prefers-reduced-motion: reduce)");
    new ResizeObserver(() => this.resize()).observe(canvas);
    this.resize();
  }

  resize() {
    const dpr = window.devicePixelRatio || 1;
    const { width, height } = this.canvas.getBoundingClientRect();
    this.canvas.width = Math.max(1, Math.round(width * dpr));
    this.canvas.height = Math.max(1, Math.round(height * dpr));
    this.dpr = dpr;
    const c = this.canvas;
    this.colours = {
      cloud: css(c, "--scr-dim"),
      sea: css(c, "--wave"),
      surface: css(c, "--accent-ui"),
      rain: css(c, "--series-4"),
      bolt: css(c, "--scr-bright"),
      storm: css(c, "--series-3"),
      grid: css(c, "--grid"),
      text: css(c, "--text-dim"),
    };
  }

  setLayout(hello) {
    this.traces = hello.traces || [];
    this.index = {};
    for (const name of NAMES) this.index[name] = this.traces.findIndex((t) => t.name === name);
    this.now = this.shown = null;
    this.history = [];
    this.bolts = [];
    this.drops = [];
  }

  addOverview() {}

  setFrame(frame, at) {
    const points = frame.traces;
    if (!points || points.length === 0 || !this.traces.length) return;
    const value = (p, name) => {
      const i = this.index[name];
      if (i < 0 || p[i] === undefined) return 0;
      const t = this.traces[i];
      return t.min + p[i] * (t.max - t.min);
    };
    // A strike is the flash jumping up anywhere in the frame's points.
    for (const p of points) {
      const flash = value(p, "Flash");
      if (flash > 50 && this.lastFlash <= 50) this.strike(at);
      this.lastFlash = flash;
    }
    const last = points[points.length - 1];
    this.now = Object.fromEntries(NAMES.map((n) => [n, value(last, n)]));
    this.shown ??= { ...this.now };
    const h = this.history;
    if (!h.length || at - h[h.length - 1].t >= HISTORY_EVERY_MS) h.push({ t: at, storm: this.now.Storm });
    while (h.length && at - h[0].t > MINUTE_MS) h.shift();
  }

  // A jagged bolt from the sky to somewhere on the sea.
  strike(at) {
    const x = 0.15 + 0.7 * Math.random();
    const path = [[x, 0]];
    let px = x;
    for (let y = 0.12; y < 1; y += 0.1 + 0.08 * Math.random()) {
      px += (Math.random() - 0.5) * 0.08;
      path.push([px, y]);
    }
    this.bolts.push({ at, path });
  }

  draw(now) {
    const { ctx, canvas, dpr, colours: C } = this;
    const w = canvas.width / dpr;
    const h = canvas.height / dpr;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, w, h);
    if (!this.now) {
      ctx.fillStyle = C.text;
      ctx.font = "12px ui-monospace, monospace";
      ctx.fillText("Waiting for the sea…", 8, h / 2);
      return;
    }

    const still = this.motion?.matches;
    const dt = Math.min(now - (this.lastDraw || now), 100) / 1000;
    this.lastDraw = now;
    // Ease what's shown towards the latest values: the traces come every 30 ms or so.
    const ease = 1 - Math.exp(-dt * 8);
    for (const n of NAMES) this.shown[n] += ease * (this.now[n] - this.shown[n]);
    const S = this.shown;
    const storm = S.Storm / 100;
    const wind = S.Wind / 100;
    const t = still ? 0 : now / 1000;

    // The storm's last minute, along the top.
    const top = 4;
    const graphH = h * 0.22;
    ctx.strokeStyle = C.grid;
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(0, top + graphH + 0.5);
    ctx.lineTo(w, top + graphH + 0.5);
    ctx.stroke();
    if (this.history.length > 1) {
      ctx.strokeStyle = C.storm;
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      this.history.forEach((p, i) => {
        const x = w - ((now - p.t) / MINUTE_MS) * w;
        const y = top + graphH * (1 - p.storm / 100);
        i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
      });
      ctx.stroke();
    }
    ctx.fillStyle = C.text;
    ctx.font = "10px ui-monospace, monospace";
    ctx.fillText("storm · last minute", 6, top + 10);

    // Clouds gather as the storm rises.
    if (storm > 0.02) {
      ctx.fillStyle = C.cloud;
      for (let i = 0; i < 6; i++) {
        const cx = ((i * 0.19 + t * (0.01 + 0.03 * wind) * (1 + (i % 3))) % 1.3) * w - 0.15 * w;
        const cy = h * (0.3 + 0.05 * (i % 3));
        ctx.globalAlpha = 0.18 * storm;
        ctx.beginPath();
        ctx.ellipse(cx, cy, w * 0.14, h * 0.06, 0, 0, Math.PI * 2);
        ctx.fill();
      }
      ctx.globalAlpha = 1;
    }

    // The surface: higher as the sea is louder, swelling with Tide,
    // choppier with wind, shaken by thunder.
    const level = Math.max(0, Math.min(1, (S.Sea + 60) / 60));
    const base = h * (0.92 - 0.42 * level) - S.Tide * 1.5;
    const amp = 1.5 + 7 * wind + 3 * storm;
    const shake = (S.Thunder / 100) * 3;
    const surface = (x) => {
      const k = (x / w) * Math.PI * 2;
      return (
        base +
        amp * (Math.sin(k * 2 - t * 0.9) * 0.6 + Math.sin(k * 5.3 + t * (1.3 + 2 * wind)) * 0.3 * (0.3 + wind)) +
        (shake && !still ? (Math.random() - 0.5) * shake : 0)
      );
    };
    ctx.beginPath();
    ctx.moveTo(0, h);
    for (let x = 0; x <= w; x += 4) ctx.lineTo(x, surface(x));
    ctx.lineTo(w, h);
    ctx.closePath();
    ctx.fillStyle = C.sea;
    ctx.fill();
    ctx.strokeStyle = C.surface;
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    for (let x = 0; x <= w; x += 4) x ? ctx.lineTo(x, surface(x)) : ctx.moveTo(x, surface(x));
    ctx.stroke();

    // Rain, slanting with the wind.
    const rain = S.Rain / 100;
    if (!still) {
      let spawn = rain * rain * 120 * dt + (rain > 0.01 ? 2 * dt : 0);
      while (spawn > 0 && this.drops.length < MAX_DROPS) {
        if (spawn < 1 && Math.random() > spawn) break;
        this.drops.push({ x: Math.random() * w * 1.2, y: top + graphH, v: 120 + 80 * Math.random() });
        spawn -= 1;
      }
      const slant = 30 + 120 * wind;
      for (const d of this.drops) {
        d.y += d.v * dt;
        d.x -= slant * dt;
      }
      this.drops = this.drops.filter((d) => d.y < surface(Math.max(0, Math.min(w, d.x))));
    }
    if (this.drops.length) {
      ctx.strokeStyle = C.rain;
      ctx.globalAlpha = 0.7;
      ctx.lineWidth = 1;
      ctx.beginPath();
      const lean = (30 + 120 * wind) / 160;
      for (const d of this.drops) {
        ctx.moveTo(d.x, d.y);
        ctx.lineTo(d.x + 5 * lean, d.y - 5);
      }
      ctx.stroke();
      ctx.globalAlpha = 1;
    }

    // Lightning: a bolt to the sea, and (but with reduced motion) the sky lit.
    this.bolts = this.bolts.filter((b) => now - b.at < BOLT_MS);
    for (const b of this.bolts) {
      const age = (now - b.at) / BOLT_MS;
      if (!still && age < 0.3) {
        ctx.fillStyle = C.bolt;
        ctx.globalAlpha = 0.25 * (1 - age / 0.3);
        ctx.fillRect(0, 0, w, h);
      }
      ctx.globalAlpha = 1 - age;
      ctx.strokeStyle = C.bolt;
      ctx.lineWidth = 2;
      ctx.beginPath();
      const seaY = surface(b.path[0][0] * w);
      b.path.forEach(([x, y], i) => {
        const py = top + graphH + y * (seaY - top - graphH);
        i ? ctx.lineTo(x * w, py) : ctx.moveTo(x * w, py);
      });
      ctx.stroke();
      ctx.globalAlpha = 1;
    }
  }

  status() {
    if (!this.now) return "";
    const parts = [`storm ${Math.round(this.now.Storm)}%`];
    if (this.now.Rain > 1) parts.push("rain");
    if (this.now.Thunder > 1) parts.push("thunder");
    return parts.join(" · ");
  }
}

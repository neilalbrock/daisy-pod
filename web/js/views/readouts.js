// The small readouts under the buffer view: tempo, level meters and CPU.

const DB_FLOOR = -60;
const HOLD_MS = 1500;
const CPU_HISTORY_MS = 30000;

function css(name) {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}

function toDb(level) {
  return level > 0 ? 20 * Math.log10(level) : -Infinity;
}

// Tempo, its source, and where we are in the bar and the beat.
export class TempoView {
  constructor(root) {
    this.root = root;
    root.innerHTML = `
      <div class="panel-head"><h2>Tempo</h2><span class="panel-meta tempo-source">–</span></div>
      <div class="tempo-bpm"><span class="tempo-value">–</span> <span class="unit">BPM</span></div>
      <div class="tempo-beats" aria-hidden="true"></div>
      <div class="tempo-steps" aria-hidden="true"></div>`;
    this.value = root.querySelector(".tempo-value");
    this.source = root.querySelector(".tempo-source");
    this.beats = root.querySelector(".tempo-beats");
    this.steps = root.querySelector(".tempo-steps");
    this.beatDots = [];
    for (let i = 0; i < 4; i++) {
      const d = document.createElement("span");
      this.beats.append(d);
      this.beatDots.push(d);
    }
    this.stepDots = [];
    this.tempo = null;
    this.at = 0;
  }

  // No tempo (driftnet before Synced is on): keep the panel, dim it.
  idle() {
    if (this.root.classList.contains("idle") && !this.tempo) return;
    this.tempo = null;
    this.root.classList.add("idle");
    this.value.textContent = "–";
    this.source.textContent = "Not synced";
    this.source.classList.remove("clock");
    for (const d of [...this.beatDots, ...this.stepDots]) d.classList.remove("on");
  }

  update(tempo, at) {
    this.tempo = tempo;
    this.at = at;
    this.root.classList.remove("idle");
    this.value.textContent = tempo.bpm.toFixed(1);
    this.source.textContent = tempo.following ? "MIDI clock" : "Tap tempo";
    this.source.classList.toggle("clock", tempo.following);
    const n = Math.max(1, Math.round(tempo.stepsPerBeat));
    if (n !== this.stepDots.length) {
      this.steps.replaceChildren();
      this.stepDots = [];
      for (let i = 0; i < Math.min(n, 8); i++) {
        const d = document.createElement("span");
        this.steps.append(d);
        this.stepDots.push(d);
      }
    }
  }

  draw(now) {
    const t = this.tempo;
    if (!t) return;
    const beat = t.beat + (Math.max(0, Math.min(now - this.at, 120)) * t.bpm) / 60000;
    const inBar = Math.floor(beat) % 4;
    this.beatDots.forEach((d, i) => d.classList.toggle("on", i === inBar));
    const frac = beat - Math.floor(beat);
    if (t.stepsPerBeat >= 1) {
      const step = Math.floor(frac * this.stepDots.length);
      this.stepDots.forEach((d, i) => d.classList.toggle("on", i === step));
    } else {
      // Slower than the beat: a step lasts several beats.
      const per = Math.round(1 / t.stepsPerBeat);
      this.stepDots.forEach((d) => d.classList.toggle("on", Math.floor(beat) % per === 0));
    }
  }
}

// Peak meters in dB, with a falling peak hold.
export class Meters {
  constructor(root) {
    this.root = root;
    this.bars = [];
  }

  setNames(names) {
    this.root.innerHTML = `<div class="panel-head"><h2>Levels</h2><span class="panel-meta">peak dBFS</span></div><div class="meters"></div>`;
    const wrap = this.root.querySelector(".meters");
    this.bars = names.map((name) => {
      const m = document.createElement("div");
      m.className = "meter";
      // The track is the whole scale, coloured by level; the cover hides what's above the
      // level, so yellow and red only show when the level is up there.
      m.innerHTML = `<div class="meter-track"><div class="meter-cover"></div><div class="meter-hold"></div></div>
        <div class="meter-name">${name}</div><div class="meter-db">–</div>`;
      m.setAttribute("role", "meter");
      m.setAttribute("aria-label", `${name} level`);
      m.setAttribute("aria-valuemin", String(DB_FLOOR));
      m.setAttribute("aria-valuemax", "0");
      wrap.append(m);
      return {
        el: m,
        cover: m.querySelector(".meter-cover"),
        hold: m.querySelector(".meter-hold"),
        db: m.querySelector(".meter-db"),
        level: -Infinity,
        held: -Infinity,
        heldAt: 0,
      };
    });
  }

  update(levels, now) {
    levels.forEach((l, i) => {
      const b = this.bars[i];
      if (!b) return;
      const db = toDb(l);
      // Fall back gently rather than jumping between frames.
      b.level = db > b.level ? db : Math.max(db, b.level - 1.5);
      if (b.level >= b.held || now - b.heldAt > HOLD_MS) {
        b.held = b.level;
        b.heldAt = now;
      }
    });
  }

  draw() {
    for (const b of this.bars) {
      const frac = (d) => (Number.isFinite(d) ? Math.max(0, Math.min(1, (d - DB_FLOOR) / -DB_FLOOR)) : 0);
      b.cover.style.transform = `scaleY(${1 - frac(b.level)})`;
      b.hold.style.bottom = `${frac(b.held) * 100}%`;
      b.hold.classList.toggle("over", b.held > -1);
      b.db.textContent = Number.isFinite(b.held) && b.held > DB_FLOOR ? b.held.toFixed(1) : "–∞";
      b.el.setAttribute("aria-valuenow", Number.isFinite(b.level) ? b.level.toFixed(0) : String(DB_FLOOR));
    }
  }
}

// Audio callback load, average and peak, over the last 30 seconds.
export class CpuView {
  constructor(root) {
    root.innerHTML = `
      <div class="panel-head"><h2>CPU</h2><span class="panel-meta">last 30 s</span></div>
      <div class="cpu-numbers"><span class="cpu-avg">–</span> avg · <span class="cpu-max">–</span> peak</div>
      <canvas class="cpu-graph" aria-label="CPU load over the last 30 seconds" role="img"></canvas>`;
    this.avg = root.querySelector(".cpu-avg");
    this.max = root.querySelector(".cpu-max");
    this.canvas = root.querySelector("canvas");
    this.ctx = this.canvas.getContext("2d");
    this.history = [];
    new ResizeObserver(() => this.resize()).observe(this.canvas);
    this.resize();
  }

  resize() {
    const dpr = window.devicePixelRatio || 1;
    const { width, height } = this.canvas.getBoundingClientRect();
    this.canvas.width = Math.max(1, Math.round(width * dpr));
    this.canvas.height = Math.max(1, Math.round(height * dpr));
    this.dpr = dpr;
    this.colours = { avg: css("--accent-ui"), max: css("--text-dim"), warn: css("--warn"), grid: css("--grid") };
    this.dirty = true;
  }

  update(cpu, now) {
    this.history.push({ t: now, ...cpu });
    while (this.history.length && now - this.history[0].t > CPU_HISTORY_MS) this.history.shift();
    // The peak over the last second reads better than one frame's.
    const recent = this.history.filter((h) => now - h.t < 1000);
    this.avg.textContent = `${cpu.avg}%`;
    this.max.textContent = `${Math.max(...recent.map((h) => h.max))}%`;
    this.max.classList.toggle("warn", Math.max(...recent.map((h) => h.max)) >= 80);
    this.dirty = true;
  }

  draw(now) {
    if (!this.dirty) return;
    this.dirty = false;
    const { ctx, dpr } = this;
    const w = this.canvas.width / dpr;
    const h = this.canvas.height / dpr;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, w, h);
    const y = (pct) => h - (Math.min(pct, 100) / 100) * h;
    ctx.strokeStyle = this.colours.grid;
    ctx.lineWidth = 1;
    for (const pct of [50, 80]) {
      ctx.beginPath();
      ctx.moveTo(0, Math.round(y(pct)) + 0.5);
      ctx.lineTo(w, Math.round(y(pct)) + 0.5);
      ctx.stroke();
    }
    const x = (t) => w - ((now - t) / CPU_HISTORY_MS) * w;
    const line = (key, colour, width) => {
      ctx.strokeStyle = colour;
      ctx.lineWidth = width;
      ctx.beginPath();
      this.history.forEach((p, i) => (i ? ctx.lineTo(x(p.t), y(p[key])) : ctx.moveTo(x(p.t), y(p[key]))));
      ctx.stroke();
    };
    line("max", this.colours.max, 1);
    line("avg", this.colours.avg, 2);
  }
}

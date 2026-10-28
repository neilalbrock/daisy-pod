// The display's readouts: tempo, level meters, CPU, and the latest values
// of the traces it doesn't plot.

const DB_FLOOR = -60;
const HOLD_MS = 1500;
const CPU_PEAK_MS = 1000; // the peak over the last second reads better than one frame's

function toDb(level) {
  return level > 0 ? 20 * Math.log10(level) : -Infinity;
}

// Tempo on one line: BPM, its source, and where we are in the bar.
export class TempoView {
  constructor(root) {
    this.root = root;
    root.innerHTML = `
      <span class="tempo-value">–</span>
      <span class="tempo-source">–</span>
      <span class="tempo-beats" aria-hidden="true"><span></span><span></span><span></span><span></span></span>`;
    this.value = root.querySelector(".tempo-value");
    this.source = root.querySelector(".tempo-source");
    this.beatDots = [...root.querySelectorAll(".tempo-beats span")];
    this.tempo = null;
    this.at = 0;
  }

  // No tempo (driftnet before Synced is on): keep the line, dimmed.
  idle() {
    if (this.root.classList.contains("idle") && !this.tempo) return;
    this.tempo = null;
    this.root.classList.add("idle");
    this.value.textContent = "–";
    this.source.textContent = "BPM · not synced";
    this.source.classList.remove("clock");
    for (const d of this.beatDots) d.classList.remove("on");
  }

  update(tempo, at) {
    this.tempo = tempo;
    this.at = at;
    this.root.classList.remove("idle");
    this.value.textContent = tempo.bpm.toFixed(1);
    this.source.textContent = tempo.following ? "BPM · MIDI clock" : "BPM · tap";
    this.source.classList.toggle("clock", tempo.following);
  }

  draw(now) {
    const t = this.tempo;
    if (!t) return;
    const beat = t.beat + (Math.max(0, Math.min(now - this.at, 120)) * t.bpm) / 60000;
    const inBar = Math.floor(beat) % 4;
    this.beatDots.forEach((d, i) => d.classList.toggle("on", i === inBar));
  }
}

// Peak meters in dB, one row each, with a falling peak hold.
export class Meters {
  constructor(root) {
    this.root = root;
    this.bars = [];
  }

  setNames(names) {
    this.root.replaceChildren();
    this.bars = names.map((name) => {
      const m = document.createElement("div");
      m.className = "meter";
      m.innerHTML = `<span class="meter-name"></span><span class="meter-track"><span class="meter-cover"></span><span class="meter-hold"></span></span><span class="meter-db">–∞</span>`;
      m.querySelector(".meter-name").textContent = name;
      m.setAttribute("role", "meter");
      m.setAttribute("aria-label", `${name} level`);
      m.setAttribute("aria-valuemin", String(DB_FLOOR));
      m.setAttribute("aria-valuemax", "0");
      this.root.append(m);
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
    const frac = (d) => (Number.isFinite(d) ? Math.max(0, Math.min(1, (d - DB_FLOOR) / -DB_FLOOR)) : 0);
    for (const b of this.bars) {
      b.cover.style.transform = `scaleX(${1 - frac(b.level)})`;
      b.hold.style.left = `calc(${frac(b.held) * 100}% - 2px)`;
      b.hold.classList.toggle("over", b.held > -1);
      b.db.textContent = Number.isFinite(b.held) && b.held > DB_FLOOR ? b.held.toFixed(1) : "–∞";
      b.el.setAttribute("aria-valuenow", Number.isFinite(b.level) ? b.level.toFixed(0) : String(DB_FLOOR));
    }
  }
}

// The audio callback's load: average, and the peak over the last second.
export class CpuView {
  constructor(root) {
    this.root = root;
    this.recent = [];
    this.shown = "";
  }

  update(cpu, now) {
    this.recent.push({ t: now, max: cpu.max });
    while (this.recent.length && now - this.recent[0].t > CPU_PEAK_MS) this.recent.shift();
    const peak = Math.max(...this.recent.map((r) => r.max));
    const text = `CPU ${cpu.avg}% · peak ${peak}%`;
    if (text === this.shown) return;
    this.shown = text;
    this.root.innerHTML = peak >= 80 ? `CPU ${cpu.avg}% · <span class="warn">peak ${peak}%</span>` : text;
  }
}

// The latest value of each trace the display doesn't plot, in its unit
// ("Clock 16.0 kHz · Env threshold −36 dB").
export class TraceReadouts {
  constructor(root) {
    this.root = root;
    this.items = [];
  }

  setLayout(traces, names) {
    this.root.replaceChildren();
    this.items = traces
      .map((t, i) => ({ t, i }))
      .filter(({ t }) => names.includes(t.name))
      .map(({ t, i }) => {
        const span = document.createElement("span");
        span.append(`${t.name} `);
        const b = document.createElement("b");
        b.textContent = "–";
        span.append(b);
        this.root.append(span);
        return { trace: t, index: i, el: b, shown: "" };
      });
  }

  update(points) {
    const last = points[points.length - 1];
    if (!last) return;
    for (const item of this.items) {
      const { min, max, unit } = item.trace;
      const v = min + last[item.index] * (max - min);
      const digits = max - min > 50 ? 0 : 1;
      // The unit, unless it only repeats the name ("Voices 4").
      const shown = unit.toLowerCase() === item.trace.name.toLowerCase() ? "" : ` ${unit}`;
      const text = `${v.toFixed(digits).replace("-", "−")}${shown}`;
      if (text !== item.shown) item.el.textContent = item.shown = text;
    }
  }
}

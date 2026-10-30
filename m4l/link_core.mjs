// Link core - a Max for Live device's side of the web link, without Max
//
// Opens the Daisy's USB MIDI ports directly (Live passes no SysEx through a
// track), asks the app for everything and keeps it talking with a keep-alive
// a second, as the web page does (web/js/link.js). It keeps the Pod's state,
// and maps between the device's controls, which hold Live values, and the
// app's parameters, which hold raw ones, by the device's spec (written by
// tools/build.mjs next to the device).
//
// Syncing: once the device knows the values Live restored, and the Pod has
// sent its whole state, a device that has synced before (a Live set) sends
// its values to the Pod, like a plugin restoring its state; a new one takes
// the Pod's instead. After that each side follows the other. Wavebyte's
// device holds only the edited layer, so it always takes the Pod's.
//
// It also keeps what the Pod streams for the device's display (traces, the
// buffer, tempo, levels) with the values the display names, as
// displayState(); displayDirty says when that has changed. A section page
// that follows a control (wavebyte's effect, by its slots' types) is told
// to show the page that control picks.
//
// out: { param(id, value), status(text), presets(slot, modified, used),
//        synced(), page(key, page), log(text) }

import midi from "@julusian/midi";

import { UNIT_MAX, decode, keepAlive, presetSave, presetSelect, request, set } from "../web/js/protocol.js";

const KEEP_ALIVE_MS = 1000;
const SILENCE_MS = 3000; // nothing heard: close and look for the Pod again
const SCAN_MS = 2000;
const GUARD_MS = 600; // ignore the Pod's value for a control this long after moving it
const PUSH_BATCH = 8; // sets per tick when sending everything (the Pod's USB FIFO is small)
const PUSH_TICK_MS = 5;
// A Pod knob's reading flickers by a step or two at rest. Following it would
// rewrite the Live parameter over and over (filling the undo history and
// overriding automation), so a knob must move more than this (0.1%) first.
const KNOB_DEADBAND = 16;
const TRACE_POINTS = 480; // kept per trace for the display
const DISPLAY_COLUMNS = 256; // the buffer's overview, for the display

export const PORT_PATTERN = /daisy/i;

// The raw value for a control's Live value, and back.
export function toRaw(control, value) {
  switch (control.kind) {
    case "dial":
      return Math.round((Math.max(0, Math.min(100, value)) / 100) * UNIT_MAX);
    case "menu": // a continuous parameter picking from a list: the middle of its step
      return Math.round(((value + 0.5) / control.n) * UNIT_MAX);
    case "toggle":
    case "trigger":
      return value ? 1 : 0;
    default:
      return Math.round(value);
  }
}

export function fromRaw(control, raw) {
  switch (control.kind) {
    case "dial":
      return (raw / UNIT_MAX) * 100;
    case "menu":
      return Math.min(control.n - 1, Math.floor((raw / UNIT_MAX) * control.n));
    default:
      return raw;
  }
}

// Whether two raw values show the same on the control.
function same(control, a, b) {
  switch (control.kind) {
    case "dial":
      return Math.abs(a - b) <= KNOB_DEADBAND;
    case "menu":
      return fromRaw(control, a) === fromRaw(control, b);
    default:
      return a === b;
  }
}

// Picks the Pod's ports by name, opens them and reassembles SysEx.
export class Ports {
  constructor(onMessage) {
    this.input = null;
    this.output = null;
    this.name = "";
    this.onMessage = onMessage;
    this.pending = null;
  }

  // The Pod's port names, if it's there.
  static find() {
    const input = new midi.Input();
    const output = new midi.Output();
    const pick = (port) => {
      for (let i = 0; i < port.getPortCount(); i++) if (PORT_PATTERN.test(port.getPortName(i))) return i;
      return -1;
    };
    const found = { input: pick(input), output: pick(output) };
    found.name = found.input >= 0 ? input.getPortName(found.input) : "";
    input.closePort();
    output.closePort();
    return found.input >= 0 && found.output >= 0 ? found : null;
  }

  open() {
    const found = Ports.find();
    if (!found) return false;
    this.input = new midi.Input();
    this.input.ignoreTypes(false, true, true); // SysEx in; no clock or active sensing
    this.input.on("message", (_, bytes) => this.receive(bytes));
    this.input.openPort(found.input);
    this.output = new midi.Output();
    this.output.openPort(found.output);
    this.name = found.name;
    return true;
  }

  close() {
    this.input?.closePort();
    this.output?.closePort();
    this.input = this.output = null;
    this.pending = null;
  }

  get isOpen() {
    return this.output !== null;
  }

  send(bytes) {
    this.output?.sendMessage(bytes);
  }

  // A SysEx message may come in pieces.
  receive(bytes) {
    if (bytes[0] === 0xf0) this.pending = [];
    else if (this.pending === null) return; // not SysEx
    this.pending.push(...bytes);
    if (bytes[bytes.length - 1] === 0xf7) {
      const message = Uint8Array.from(this.pending);
      this.pending = null;
      this.onMessage(message);
    }
  }
}

export class DeviceLink {
  constructor(spec, out) {
    this.spec = spec;
    this.out = out;
    this.ports = new Ports((m) => this.receive(m));
    this.byIndex = new Map(); // a parameter's index -> its control (layer ones: layer A's)
    for (const c of spec.controls) this.byIndex.set(c.index, c);
    this.layerControl = spec.controls.find((c) => c.name === spec.layerParam) || null;

    this.byName = new Map(spec.controls.map((c) => [c.name, c]));
    this.values = new Map(); // control id -> Live value, as the device shows it
    this.restored = false; // the device has sent what Live restored
    this.synced = false; // the device has synced before (saved with the set)
    this.pushing = [];
    this.moved = new Map(); // control id -> when the device last moved it
    this.reset();

    this.timer = setInterval(() => this.tick(), KEEP_ALIVE_MS);
    this.pushTimer = setInterval(() => this.pushSome(), PUSH_TICK_MS);
    this.tick();
  }

  reset() {
    this.hello = null;
    this.wrongApp = false;
    this.pod = []; // the Pod's raw values, by parameter
    this.have = 0; // how many of them have come
    this.live = false; // initial sync done: each side follows the other
    this.lastHeard = 0;
    this.cpu = null;
    this.pushing = [];
    // For the display.
    this.stream = { traces: [], points: [], overview: null, buffer: null, tempo: null, levels: null, xy: null, presets: null };
    this.displayDirty = true;
  }

  close() {
    clearInterval(this.timer);
    clearInterval(this.pushTimer);
    this.ports.close();
  }

  // Every second: find the Pod, or keep it talking.
  tick() {
    const now = Date.now();
    if (!this.ports.isOpen) {
      if (now - (this.lastScan || 0) < SCAN_MS) return;
      this.lastScan = now;
      if (!this.ports.open()) {
        this.status("Looking for the Pod");
        return;
      }
      this.reset();
      this.lastHeard = now;
      this.status(`Asking ${this.ports.name}`);
      this.ports.send(request());
      return;
    }
    if (now - this.lastHeard > SILENCE_MS) {
      this.ports.close();
      this.reset();
      this.status("Lost the Pod");
      return;
    }
    this.ports.send(this.hello ? keepAlive() : request());
  }

  status(text) {
    if (text !== this.lastStatus) this.out.status(text), (this.displayDirty = true);
    this.lastStatus = text;
  }

  // --- From the Pod

  receive(bytes) {
    let m;
    try {
      m = decode(bytes);
    } catch {
      return; // cut short or garbled: a throw here would stop the node script
    }
    if (!m) return;
    this.lastHeard = Date.now();
    switch (m.type) {
      case "hello":
        this.reset();
        this.lastHeard = Date.now();
        this.hello = m;
        this.wrongApp = m.app !== this.spec.app;
        if (this.wrongApp) this.status(`The Pod is running ${m.app}, not ${this.spec.app}`);
        else if (m.numParams !== this.spec.numParams)
          this.status(`${m.app} has changed: run make m4l`), (this.wrongApp = true);
        else this.status(`${m.app}: syncing`);
        this.stream.traces = m.traces;
        this.stream.points = m.traces.map(() => []);
        this.stream.overview = m.overviewColumns ? new Uint8Array(m.overviewColumns) : null;
        break;
      case "params":
        // The spec has them; check its names match, so no value lands on the wrong control.
        for (const p of m.params) {
          const c = this.byIndex.get(p.index);
          if (c && c.podName !== p.name && !this.wrongApp) {
            this.wrongApp = true;
            this.status(`${this.spec.app} has changed: run make m4l`);
          }
        }
        break;
      case "state":
        if (!this.wrongApp) this.state(m.first, m.values);
        break;
      case "frame":
        if (this.wrongApp) break;
        if (m.presets) this.out.presets(m.presets.slot, m.presets.modified, m.presets.used);
        if (m.cpu) this.cpu = m.cpu;
        this.keep(m);
        if (this.live) this.status(`${this.hello.app} on ${this.ports.name}${this.cpu ? `, CPU ${this.cpu.avg}%` : ""}`);
        break;
    }
  }

  // What a frame brings for the display.
  keep(m) {
    const st = this.stream;
    if (m.traces)
      for (const p of m.traces)
        p.forEach((v, k) => {
          const line = st.points[k];
          if (!line) return;
          line.push(v);
          if (line.length > TRACE_POINTS) line.splice(0, line.length - TRACE_POINTS);
        });
    if (m.overview && st.overview) {
      const { first, values } = m.overview;
      for (let i = 0; i < values.length; i++) st.overview[(first + i) % st.overview.length] = values[i];
    }
    for (const k of ["buffer", "tempo", "levels", "xy", "presets"]) if (m[k]) st[k] = m[k];
    this.displayDirty = true;
  }

  // The display's view of it all: plain data, for JSON.
  displayState() {
    const st = this.stream;
    const h = this.hello;
    const values = {};
    for (const name of this.spec.display?.values ?? []) {
      const c = this.byName.get(name);
      const raw = c ? this.pod[this.indexOf(c)] : undefined;
      if (raw === undefined) continue;
      const v = c.kind === "dial" ? raw / UNIT_MAX : fromRaw(c, raw);
      values[name] = { v, text: c.options?.[v] };
    }
    let overview = null;
    if (st.overview) {
      const n = Math.min(DISPLAY_COLUMNS, st.overview.length);
      const per = st.overview.length / n;
      overview = Array.from({ length: n }, (_, i) => {
        let peak = 0;
        for (let k = Math.floor(i * per); k < Math.floor((i + 1) * per); k++) peak = Math.max(peak, st.overview[k]);
        return peak;
      });
    }
    return {
      app: this.spec.app,
      status: this.lastStatus ?? "",
      live: this.live && !this.wrongApp,
      cpu: this.cpu,
      bufferFrames: h?.bufferFrames ?? 0,
      tracePeriodMs: h?.tracePeriodMs ?? 0,
      traces: st.traces.map((t, k) => ({ name: t.name, unit: t.unit, min: t.min, max: t.max, points: st.points[k] })),
      overview,
      buffer: st.buffer,
      tempo: st.tempo,
      levels: st.levels,
      xy: st.xy,
      presets: st.presets && { slot: st.presets.slot, modified: st.presets.modified },
      values,
    };
  }

  // A section page that follows this control: show the page it picks.
  followPage(c, value) {
    for (const p of this.spec.pages ?? []) {
      if (!p.follow.includes(c.id)) continue;
      const page = p.map[value];
      if (page !== undefined) this.out.page?.(p.key, page);
    }
  }

  state(first, values) {
    const editBefore = this.editLayer();
    for (let i = 0; i < values.length; i++) {
      const index = first + i;
      if (this.pod[index] === undefined) this.have++;
      const changed = this.pod[index] !== values[i];
      this.pod[index] = values[i];
      if (changed) this.displayDirty = true;
      if (this.live && changed) this.follow(index);
    }
    if (this.live && this.editLayer() !== editBefore) this.showLayer();
    if (!this.live && this.have >= this.spec.numParams && this.restored) this.start();
  }

  // The Pod changed a parameter: show it, unless the device just moved it.
  follow(index) {
    const c = this.controlFor(index);
    if (!c || c.kind === "trigger" || this.pushing.length) return;
    if (Date.now() - (this.moved.get(c.id) ?? 0) < GUARD_MS) return;
    this.show(c);
  }

  show(c) {
    const raw = this.pod[this.indexOf(c)];
    if (raw === undefined) return;
    const shown = this.values.get(c.id);
    if (shown !== undefined && same(c, toRaw(c, shown), raw)) return;
    const value = fromRaw(c, raw);
    this.values.set(c.id, value);
    this.out.param(c.id, value);
    this.followPage(c, value);
  }

  // --- Layers (wavebyte): layer controls edit the layer the Pod edits

  editLayer() {
    if (!this.layerControl) return 0;
    return this.pod[this.layerControl.index] ?? 0;
  }

  indexOf(c) {
    return c.layer ? this.editLayer() * this.spec.layerParams + c.index : c.index;
  }

  controlFor(index) {
    const L = this.spec.layerParams;
    if (L && index < L * this.spec.layers) {
      if (Math.floor(index / L) !== this.editLayer()) return null;
      return this.byIndex.get(index % L) || null;
    }
    return this.byIndex.get(index) || null;
  }

  showLayer() {
    for (const c of this.spec.controls) if (c.layer) this.show(c);
  }

  // --- Initial sync

  start() {
    this.live = true;
    if (this.synced && !this.spec.pullOnly) {
      this.pushAll();
    } else {
      this.pullAll();
      this.synced = true;
      this.out.synced();
    }
  }

  pullAll() {
    // Layer first, so layer controls show the layer it picks.
    if (this.layerControl) this.show(this.layerControl);
    for (const c of this.spec.controls) if (c.kind !== "trigger") this.show(c);
  }

  pushAll() {
    this.pushing = [];
    if (this.layerControl && this.values.has(this.layerControl.id)) this.pushing.push(this.layerControl);
    for (const c of this.spec.controls)
      if (c.kind !== "trigger" && c !== this.layerControl && this.values.has(c.id)) this.pushing.push(c);
  }

  pushSome() {
    if (!this.pushing.length) return;
    if (!this.ports.isOpen) return void (this.pushing = []);
    for (const c of this.pushing.splice(0, PUSH_BATCH)) this.send(c, this.values.get(c.id));
  }

  send(c, value) {
    const index = this.indexOf(c);
    const raw = toRaw(c, value);
    this.ports.send(set(index, raw));
    if (c.kind === "trigger") return;
    this.pod[index] = raw;
    this.moved.set(c.id, Date.now());
  }

  // --- From the device

  // Live's values, before the node script was running: the device keeps them.
  restore(id, value) {
    this.values.set(id, value);
  }

  restoreDone(synced) {
    this.synced = !!synced;
    this.restored = true;
    if (!this.live && this.hello && !this.wrongApp && this.have >= this.spec.numParams) this.start();
  }

  control(id, value) {
    const c = this.spec.controls[id];
    if (!c) return;
    if (c.kind === "trigger") {
      if (value && this.live) this.send(c, 1);
      return;
    }
    this.values.set(id, value);
    this.moved.set(id, Date.now());
    this.followPage(c, value);
    if (!this.live) return;
    this.send(c, value);
    if (c === this.layerControl) this.showLayer();
  }

  // Take the Pod's values, e.g. after loading one of its presets.
  pull() {
    if (this.live) this.pullAll();
  }

  presetSelect(slot) {
    if (this.live) this.ports.send(presetSelect(slot));
  }

  presetSave(slot) {
    if (this.live) this.ports.send(presetSave(slot));
  }
}


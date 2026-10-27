// Web link protocol: the page's half of common/web_link.h, which it must
// match. Every message is F0 7D 02 <type> <payload> F7. Numbers are 7, 14
// or 21 bits, most significant 7 bits first; strings are 7-bit ASCII ending
// in 0.

export const ID = 0x7d;
export const VERSION = 2; // 2: 14-bit parameter indices

export const Msg = {
  HELLO: 0x01,
  PARAM: 0x02,
  STATE: 0x03,
  FRAME: 0x04,
  REQUEST: 0x40,
  SET: 0x41,
  KEEP_ALIVE: 0x42,
  PRESET_SELECT: 0x43,
  PRESET_SAVE: 0x44,
  PRESET_RESET: 0x45,
};

export const Kind = { CONTINUOUS: 0, CHOICE: 1, TOGGLE: 2, TRIGGER: 3 };

const Section = { TEMPO: 1, LEVELS: 2, CPU: 3, BUFFER: 4, OVERVIEW: 5, TRACES: 6, XY: 7, PRESETS: 8 };

export const UNIT_MAX = 16383;

class Reader {
  constructor(data, pos) {
    this.data = data;
    this.pos = pos;
  }
  get more() {
    return this.pos < this.data.length;
  }
  u7() {
    if (this.pos >= this.data.length) throw new Error("message too short");
    return this.data[this.pos++];
  }
  u14() {
    return (this.u7() << 7) | this.u7();
  }
  u21() {
    return (this.u7() << 14) | (this.u7() << 7) | this.u7();
  }
  str() {
    let s = "";
    for (let c = this.u7(); c !== 0; c = this.u7()) s += String.fromCharCode(c);
    return s;
  }
}

function readHead(r) {
  const start = r.u21();
  const length = r.u21();
  const pos = r.u21();
  const rate = r.u14() / 1000;
  const flags = r.u7();
  return {
    start,
    length,
    pos,
    rate,
    reverse: (flags & 1) !== 0,
    tap: (flags & 2) !== 0,
    amp: r.u7() / 127,
    pan: r.u7() / 127,
  };
}

function readFrame(r) {
  const frame = {};
  while (r.more) {
    const tag = r.u7();
    switch (tag) {
      case Section.TEMPO:
        frame.tempo = {
          bpm: r.u14() / 10,
          beat: r.u21() / 1000,
          stepsPerBeat: r.u7() / 4,
          following: r.u7() !== 0,
        };
        break;
      case Section.LEVELS: {
        const n = r.u7();
        frame.levels = [];
        for (let i = 0; i < n; i++) frame.levels.push(r.u14() / UNIT_MAX);
        break;
      }
      case Section.CPU:
        frame.cpu = { avg: r.u7(), max: r.u7() };
        break;
      case Section.BUFFER: {
        const write = r.u21();
        const onsets = [];
        for (let i = r.u7(); i > 0; i--) onsets.push(r.u21());
        const heads = [];
        for (let i = r.u7(); i > 0; i--) heads.push(readHead(r));
        frame.buffer = { write, onsets, heads };
        break;
      }
      case Section.OVERVIEW: {
        const first = r.u14();
        const n = r.u7();
        const values = new Uint8Array(n);
        for (let i = 0; i < n; i++) values[i] = r.u7();
        frame.overview = { first, values };
        break;
      }
      case Section.TRACES: {
        // Raw 0..1 across each trace's range; the hello has the ranges.
        const points = r.u7();
        const traces = r.u7();
        frame.traces = [];
        for (let i = 0; i < points; i++) {
          const p = [];
          for (let k = 0; k < traces; k++) p.push(r.u14() / UNIT_MAX);
          frame.traces.push(p);
        }
        break;
      }
      case Section.XY: {
        const n = r.u7();
        frame.xy = [];
        for (let i = 0; i < n; i++) frame.xy.push([(r.u7() - 63) / 63, (r.u7() - 63) / 63]);
        break;
      }
      case Section.PRESETS: {
        const slot = r.u7();
        const modified = r.u7() !== 0;
        const mask = r.u14();
        frame.presets = { slot: slot === 127 ? -1 : slot, modified, used: Array.from({ length: 14 }, (_, i) => (mask & (1 << i)) !== 0) };
        break;
      }
      default:
        // A section this page doesn't know: the rest can't be read.
        return frame;
    }
  }
  return frame;
}

// Decodes one SysEx message (with its F0 and F7), or returns null if it
// isn't ours.
export function decode(bytes) {
  if (bytes.length < 5 || bytes[0] !== 0xf0 || bytes[1] !== ID || bytes[2] !== VERSION)
    return null;
  const end = bytes[bytes.length - 1] === 0xf7 ? bytes.length - 1 : bytes.length;
  const r = new Reader(bytes.subarray(0, end), 4);
  switch (bytes[3]) {
    case Msg.HELLO: {
      const hello = {
        type: "hello",
        app: r.str(),
        sampleRate: r.u21(),
        bufferFrames: r.u21(),
        overviewColumns: r.u14(),
      };
      hello.groups = [];
      for (let i = r.u7(); i > 0; i--) hello.groups.push(r.str());
      hello.meters = [];
      for (let i = r.u7(); i > 0; i--) hello.meters.push(r.str());
      hello.numParams = r.u14();
      // Added later: traces and an X-Y plot, if the app has them.
      hello.traces = [];
      hello.tracePeriodMs = 1;
      hello.xy = "";
      hello.presets = 0;
      if (r.more) {
        const n = r.u7();
        hello.tracePeriodMs = r.u7();
        for (let i = 0; i < n; i++)
          hello.traces.push({ name: r.str(), unit: r.str(), min: r.u14() - 8192, max: r.u14() - 8192 });
        hello.xy = r.str();
      }
      // Added later still: how many preset slots (common/presets.h).
      if (r.more) hello.presets = r.u7();
      return hello;
    }
    case Msg.PARAM: {
      // As many descriptions as fit.
      const params = [];
      while (r.more) {
        const param = { index: r.u14(), kind: r.u7(), group: r.u7(), columns: r.u7(), name: r.str() };
        param.options = [];
        for (let i = r.u7(); i > 0; i--) param.options.push(r.str());
        params.push(param);
      }
      return { type: "params", params };
    }
    case Msg.STATE: {
      // A run of the values, from parameter `first`.
      const first = r.u14();
      const values = [];
      for (let i = r.u14(); i > 0; i--) values.push(r.u14());
      return { type: "state", first, values };
    }
    case Msg.FRAME:
      return { type: "frame", ...readFrame(r) };
    default:
      return null;
  }
}

function message(type, payload = []) {
  return [0xf0, ID, VERSION, type, ...payload, 0xf7];
}

export const request = () => message(Msg.REQUEST);
export const keepAlive = () => message(Msg.KEEP_ALIVE);
export const presetSelect = (slot) => message(Msg.PRESET_SELECT, [slot & 0x7f]);
export const presetSave = (slot) => message(Msg.PRESET_SAVE, [slot & 0x7f]);
export const presetReset = () => message(Msg.PRESET_RESET);

// raw: 0..16383 for continuous values, the option index for choices, 0 or 1
// for toggles.
export function set(index, raw) {
  raw = Math.max(0, Math.min(UNIT_MAX, Math.round(raw)));
  return message(Msg.SET, [(index >> 7) & 0x7f, index & 0x7f, raw >> 7, raw & 0x7f]);
}

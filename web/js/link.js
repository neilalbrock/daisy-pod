// The USB MIDI connection to the Pod, through Web MIDI. Sends a request on
// connect and a keep-alive every second (the Pod stops streaming three
// seconds after the last one), and dispatches decoded messages as
// "message" events.

import { decode, keepAlive, request } from "./protocol.js";

const KEEP_ALIVE_MS = 1000;
const SILENCE_MS = 2000; // no hello by then: ask again
const STORE_KEY = "daisy-pod-web:ports";

export class Link extends EventTarget {
  constructor() {
    super();
    this.access = null;
    this.input = null;
    this.output = null;
    this.timer = 0;
    this.lastHeard = 0;
    this.haveHello = false;
  }

  static supported() {
    return typeof navigator !== "undefined" && "requestMIDIAccess" in navigator;
  }

  async open() {
    this.access = await navigator.requestMIDIAccess({ sysex: true });
    this.access.onstatechange = () => this.dispatchEvent(new Event("ports"));
  }

  ports() {
    const list = (map) => [...map.values()].map((p) => ({ id: p.id, name: p.name }));
    return { inputs: list(this.access.inputs), outputs: list(this.access.outputs) };
  }

  // The ports used last time, or ones that look like a Daisy.
  preferred() {
    const { inputs, outputs } = this.ports();
    let saved = {};
    try {
      saved = JSON.parse(localStorage.getItem(STORE_KEY) || "{}");
    } catch {}
    const pick = (list, name) =>
      list.find((p) => p.name === name) || list.find((p) => /daisy/i.test(p.name)) || list[0];
    return { input: pick(inputs, saved.input), output: pick(outputs, saved.output) };
  }

  connect(inputId, outputId) {
    this.disconnect();
    this.input = this.access.inputs.get(inputId) || null;
    this.output = this.access.outputs.get(outputId) || null;
    if (!this.input || !this.output) return false;
    try {
      localStorage.setItem(STORE_KEY, JSON.stringify({ input: this.input.name, output: this.output.name }));
    } catch {}

    this.input.onmidimessage = (e) => this.receive(e.data);
    this.haveHello = false;
    this.lastHeard = 0;
    this.send(request());
    let asked = performance.now();
    this.timer = setInterval(() => {
      const now = performance.now();
      this.send(keepAlive());
      // Gone quiet (unplugged, reset or reflashed): start again.
      if (this.haveHello && now - this.lastHeard > SILENCE_MS) this.haveHello = false;
      if (!this.haveHello && now - asked > SILENCE_MS) {
        this.send(request());
        asked = now;
      }
      this.dispatchEvent(new Event("status"));
    }, KEEP_ALIVE_MS);
    this.dispatchEvent(new Event("status"));
    return true;
  }

  disconnect() {
    clearInterval(this.timer);
    if (this.input) this.input.onmidimessage = null;
    this.input = this.output = null;
    this.haveHello = false;
    this.dispatchEvent(new Event("status"));
  }

  get connected() {
    return this.output !== null;
  }

  // Hearing from the Pod in the last couple of seconds.
  get live() {
    return this.connected && this.haveHello && performance.now() - this.lastHeard < SILENCE_MS;
  }

  send(bytes) {
    if (!this.output) return;
    try {
      this.output.send(bytes);
    } catch (err) {
      console.warn("MIDI send failed", err);
    }
  }

  receive(data) {
    if (data[0] !== 0xf0) return; // CCs, clock and so on: not for us
    let msg;
    try {
      msg = decode(data);
    } catch (err) {
      console.warn("Bad message from the Pod", err);
      return;
    }
    if (!msg) return;
    this.lastHeard = performance.now();
    if (msg.type === "hello") this.haveHello = true;
    else if (!this.haveHello) return; // mid-stream: wait for the hello we asked for
    this.dispatchEvent(new CustomEvent("message", { detail: msg }));
  }
}

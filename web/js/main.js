// Wires the page together: the MIDI link, the faceplate built from the
// app's parameters, and the display, redrawn every animation frame.

import { Controls } from "./controls.js";
import { Link } from "./link.js";
import { PresetsPanel } from "./presets.js";
import { presetReset, presetSave, presetSelect, set } from "./protocol.js";
import { BufferView } from "./views/buffer.js";
import { CpuView, Meters, TempoView, TraceReadouts } from "./views/readouts.js";
import { TraceStrip, XYView } from "./views/traces.js";

const $ = (sel) => document.querySelector(sel);

const link = new Link();
const bufferView = new BufferView($("#scope"));
let view = bufferView; // or the app's own view of its buffer (extras.View)
const appViews = {};
const tempo = new TempoView($("#tempo"));
const meters = new Meters($("#levels"));
const cpu = new CpuView($("#cpu"));
const traces = new TraceStrip($("#traces"), $("#display-meta"));
const readouts = new TraceReadouts($("#readouts"));
const xy = new XYView($("#xy"));
const controls = new Controls(
  { surface: $("#surface"), feature: $("#feature"), below: $("#below"), rail: $("#rail"), headerExtra: $("#header-extra") },
  (index, raw) => link.send(set(index, raw)),
);
const presets = new PresetsPanel($("#presets"), $("#preset-reset"), {
  onSelect: (slot) => link.send(presetSelect(slot)),
  onSave: (slot) => link.send(presetSave(slot)),
  onReset: () => link.send(presetReset()),
});

let hello = null;
let params = [];
let app = Promise.resolve({}); // the app's extras, loading
let extras = {}; // ... and once they've loaded
let lastState = null;

// ---------------------------------------------------------------- Messages

async function loadApp(name) {
  if (!/^[a-z0-9_-]+$/.test(name)) return {};
  try {
    return (await import(`./apps/${name}.js`)).default;
  } catch {
    return {}; // no extras for this app: plain controls
  }
}

// What the display shows: the buffer (or the app's view of it), else the
// traces the app picks; the traces it doesn't plot show as numbers. An
// app's View that draws from the traces (View.fromTraces, stormsea's sea)
// shows too, with no buffer.
function setUpDisplay(msg, ex) {
  const buffer = msg.bufferFrames > 0;
  const scope = buffer || !!ex.View?.fromTraces;
  const plotted = buffer ? [] : (ex.displayTraces ?? msg.traces.slice(0, 2).map((t) => t.name));
  $("#scope").hidden = !scope;
  $("#scope").setAttribute("aria-label", ex.scopeLabel || "The sample buffer over time, with the voices and delay taps reading from it");
  $("#traces").hidden = plotted.length === 0;
  $("#display-title").textContent = scope ? ex.scopeTitle || "Buffer · last 8 s" : ex.tracesTitle || (msg.traces.length ? "Traces · last 8 s" : "Levels");
  $("#display-meta").replaceChildren();
  traces.setLayout(msg, plotted);
  const rest = msg.traces.map((t) => t.name).filter((n) => !plotted.includes(n));
  readouts.setLayout(msg.traces, ex.readouts ? rest.filter((n) => ex.readouts.includes(n)) : rest, ex.formatTrace);
  $("#xy").hidden = !msg.xy;
  $("#xy").setAttribute("aria-label", `${msg.xy}: input against output`);
}

link.addEventListener("message", async (e) => {
  const msg = e.detail;
  const now = performance.now();
  switch (msg.type) {
    case "hello":
      hello = msg;
      params = new Array(msg.numParams);
      lastState = null;
      app = loadApp(msg.app);
      $("#app-name").textContent = msg.app;
      $("#app-tagline").textContent = "";
      document.title = `${msg.app} · Daisy Pod`;
      view.setLayout(msg);
      xy.clear();
      meters.setNames(msg.meters);
      for (const id of ["#feature", "#below", "#rail", "#header-extra"]) $(id).replaceChildren();
      presets.setup(msg.presets);
      $("#tempo").hidden = true; // until a frame brings a tempo, or the app says it has one
      tempo.idle();
      extras = {};
      setUpDisplay(msg, extras);
      app.then((loaded) => {
        if (msg !== hello) return;
        extras = loaded;
        $("#app-tagline").textContent = extras.tagline || "";
        // An app can draw its buffer its own way, under its own title.
        view = extras.View ? (appViews[msg.app] ??= new extras.View($("#scope"))) : bufferView;
        view.setLayout(msg);
        setUpDisplay(msg, extras);
        // An app whose tempo comes and goes keeps its line, so the page doesn't jump.
        if (extras.tempo) $("#tempo").hidden = false;
      });
      break;
    case "params":
      if (!hello) return;
      for (const p of msg.params) if (p.index < params.length) params[p.index] = p;
      // Build once the whole list is in.
      if (params.filter(Boolean).length === hello.numParams) {
        const forHello = hello;
        const loaded = await app;
        if (forHello !== hello) return; // a newer hello came in meanwhile
        controls.build(hello, params, loaded);
        if (lastState) controls.state(lastState);
      }
      break;
    case "state":
      // The values come in runs: keep them all, and set what changed.
      if (!hello) return;
      lastState ??= new Array(hello.numParams);
      msg.values.forEach((raw, i) => (lastState[msg.first + i] = raw));
      controls.state(lastState);
      break;
    case "frame":
      view.setFrame(msg, now);
      if (msg.overview) view.addOverview(msg.overview);
      if (msg.tempo) {
        $("#tempo").hidden = false;
        tempo.update(msg.tempo, now);
      } else if (extras.tempo) {
        tempo.idle();
      }
      if (msg.traces) {
        traces.add(msg.traces, now);
        readouts.update(msg.traces);
      }
      if (msg.xy) xy.add(msg.xy);
      if (msg.levels) meters.update(msg.levels, now);
      if (msg.cpu) cpu.update(msg.cpu, now);
      if (msg.presets) presets.update(msg.presets);
      break;
  }
});

// ---------------------------------------------------------------- Connection

const inputSel = $("#input");
const outputSel = $("#output");
const status = $("#status");

function fillPorts() {
  const { inputs, outputs } = link.ports();
  const fill = (sel, list) => {
    const keep = sel.value;
    sel.replaceChildren(...list.map((p) => new Option(p.name, p.id)));
    if (list.some((p) => p.id === keep)) sel.value = keep;
  };
  fill(inputSel, inputs);
  fill(outputSel, outputs);
}

function showStatus() {
  const state = link.live ? "live" : link.connected ? "waiting" : "off";
  if (state === "live" && status.dataset.state !== "live") $("#setup").open = false; // found it: tuck Setup away
  status.dataset.state = state;
  status.querySelector(".status-text").textContent = { live: "Connected", waiting: "Waiting for the Pod…", off: "Not connected" }[state];
  const port = inputSel.selectedOptions[0]?.textContent;
  status.title = link.connected && port ? `MIDI: ${port}` : "";
  $("#connect").textContent = link.connected ? "Disconnect" : "Connect";
  document.body.classList.toggle("is-live", link.live);
}

$("#connect").addEventListener("click", () => {
  if (link.connected) link.disconnect();
  else link.connect(inputSel.value, outputSel.value);
});
for (const sel of [inputSel, outputSel])
  sel.addEventListener("change", () => link.connected && link.connect(inputSel.value, outputSel.value));

link.addEventListener("status", showStatus);
link.addEventListener("ports", fillPorts);

async function start() {
  if (!Link.supported()) {
    $("#notice").hidden = false;
    $("#notice").textContent =
      "This browser has no Web MIDI. Use Chrome, Edge or Opera on a computer, opened from localhost (make web).";
    showStatus();
    return;
  }
  try {
    await link.open();
  } catch (err) {
    $("#notice").hidden = false;
    $("#notice").textContent = `MIDI access was refused (${err.message}). Allow MIDI and SysEx for this page, then reload.`;
    showStatus();
    return;
  }
  fillPorts();
  const pref = link.preferred();
  if (pref.input) inputSel.value = pref.input.id;
  if (pref.output) outputSel.value = pref.output.id;
  if (pref.input && pref.output && /daisy/i.test(pref.input.name)) link.connect(pref.input.id, pref.output.id);
  else $("#setup").open = true; // no Daisy found: show the ports to pick from
  showStatus();
}

// ---------------------------------------------------------------- Drawing

const displayMeta = $("#display-meta");

function frame() {
  // performance.now(), the clock the frames were stamped with (the
  // callback's own timestamp can lag it).
  const now = performance.now();
  if (!$("#scope").hidden) {
    view.draw(now);
    // A view can say what it's showing, in the display's head ("Playing · 1.37 s at 16k").
    if (view.status) {
      const meta = view.status();
      if (displayMeta.textContent !== meta) displayMeta.textContent = meta;
    }
  }
  if (!$("#traces").hidden) traces.draw(now);
  if (!$("#xy").hidden) xy.draw();
  tempo.draw(now);
  meters.draw();
  requestAnimationFrame(frame);
}

start();
requestAnimationFrame(frame);

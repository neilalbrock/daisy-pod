// Wires the page together: the MIDI link, the control surface built from
// the app's parameters, and the views, redrawn every animation frame.

import { Controls } from "./controls.js";
import { Link } from "./link.js";
import { PresetsPanel } from "./presets.js";
import { presetReset, presetSave, presetSelect, set } from "./protocol.js";
import { BufferView } from "./views/buffer.js";
import { CpuView, Meters, TempoView } from "./views/readouts.js";
import { TraceView, XYView } from "./views/traces.js";

const $ = (sel) => document.querySelector(sel);

const link = new Link();
const bufferView = new BufferView($("#buffer"));
let view = bufferView; // or the app's own view of its buffer (extras.View)
const appViews = {};
const tempo = new TempoView($("#tempo"));
const meters = new Meters($("#levels"));
const cpu = new CpuView($("#cpu"));
const traces = new TraceView($("#traces"));
const xy = new XYView($("#xy canvas"));
const controls = new Controls($("#controls"), (index, raw) => link.send(set(index, raw)));
const presets = new PresetsPanel($("#presets"), {
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
      document.title = `${msg.app} · Daisy Pod`;
      view.setLayout(msg);
      traces.setLayout(msg);
      xy.clear();
      meters.setNames(msg.meters);
      $("#controls").replaceChildren();
      presets.setup(msg.presets);
      // Show the views this app has data for.
      $("#scope").hidden = msg.bufferFrames === 0;
      $("#trace-panel").hidden = msg.traces.length === 0;
      $("#xy").hidden = !msg.xy;
      $("#xy-title").textContent = msg.xy;
      $("#tempo").hidden = true; // until a frame brings a tempo, or the app says it has one
      tempo.idle();
      extras = {};
      app.then((loaded) => {
        if (msg !== hello) return;
        extras = loaded;
        $("#trace-title").textContent = extras.tracesTitle || "Traces";
        // An app can draw its buffer its own way, under its own title.
        view = extras.View ? (appViews[msg.app] ??= new extras.View($("#buffer"))) : bufferView;
        view.setLayout(msg);
        $("#scope h2").textContent = extras.scopeTitle || "Buffer";
        // The legend entries this app's view uses (all of them by default).
        const legend = extras.legend || ["speed", "tap", "onset"];
        for (const item of document.querySelectorAll("[data-legend]")) item.hidden = !legend.includes(item.dataset.legend);
        $("#scope .span").hidden = Boolean(extras.View);
        $("#scope-meta").hidden = !view.status;
        // An app whose tempo comes and goes keeps its panel, so the page doesn't jump.
        if (extras.tempo) $("#tempo").hidden = false;
      });
      break;
    case "param":
      if (!hello) return;
      params[msg.index] = msg;
      // Build once the whole list is in.
      if (params.filter(Boolean).length === hello.numParams) {
        const forHello = hello;
        const extras = await app;
        if (forHello !== hello) return; // a newer hello came in meanwhile
        controls.build(hello, params, extras);
        if (lastState) controls.state(lastState);
      }
      break;
    case "state":
      lastState = msg.values;
      controls.state(msg.values);
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
      if (msg.traces) traces.add(msg.traces, now);
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
  if (state === "live" && status.dataset.state !== "live") $("#ports").open = false; // found it: tuck the ports away
  status.dataset.state = state;
  status.querySelector(".status-text").textContent = { live: "Connected", waiting: "Waiting for the Pod…", off: "Not connected" }[state];
  const port = inputSel.selectedOptions[0]?.textContent;
  $("#status-port").textContent = link.connected && port ? ` · ${port}` : "";
  $("#connect").textContent = link.connected ? "Disconnect" : "Connect";
  document.body.classList.toggle("is-live", link.live);
}

$("#connect").addEventListener("click", () => {
  if (link.connected) link.disconnect();
  else link.connect(inputSel.value, outputSel.value);
});
for (const sel of [inputSel, outputSel])
  sel.addEventListener("change", () => link.connected && link.connect(inputSel.value, outputSel.value));

// The time-span buttons over the buffer and trace views.
function spanButtons(attr, target) {
  const buttons = document.querySelectorAll(`[${attr}]`);
  for (const b of buttons)
    b.addEventListener("click", () => {
      target().spanSeconds = Number(b.getAttribute(attr));
      for (const o of buttons) o.setAttribute("aria-pressed", String(o === b));
    });
}
spanButtons("data-span", () => view);
spanButtons("data-trace-span", () => traces);

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
  else $("#ports").open = true; // no Daisy found: show the ports to pick from
  showStatus();
}

// ---------------------------------------------------------------- Drawing

const panels = { scope: $("#scope"), traces: $("#trace-panel"), xy: $("#xy") };
const scopeMeta = $("#scope-meta");

function frame() {
  // performance.now(), the clock the frames were stamped with (the
  // callback's own timestamp can lag it).
  const now = performance.now();
  if (!panels.scope.hidden) {
    view.draw(now);
    // A view can say what it's showing, in the panel's head ("Playing · 1.37 s at 16k").
    const meta = view.status?.() ?? "";
    if (scopeMeta.textContent !== meta) scopeMeta.textContent = meta;
  }
  if (!panels.traces.hidden) traces.draw(now);
  if (!panels.xy.hidden) xy.draw();
  tempo.draw(now);
  meters.draw();
  cpu.draw(now);
  requestAnimationFrame(frame);
}

start();
requestAnimationFrame(frame);

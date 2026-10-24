// Wires the page together: the MIDI link, the control surface built from
// the app's parameters, and the views, redrawn every animation frame.

import { Controls } from "./controls.js";
import { Link } from "./link.js";
import { set } from "./protocol.js";
import { BufferView } from "./views/buffer.js";
import { CpuView, Meters, TempoView } from "./views/readouts.js";

const $ = (sel) => document.querySelector(sel);

const link = new Link();
const view = new BufferView($("#buffer"));
const tempo = new TempoView($("#tempo"));
const meters = new Meters($("#levels"));
const cpu = new CpuView($("#cpu"));
const controls = new Controls($("#controls"), (index, raw) => link.send(set(index, raw)));

let hello = null;
let params = [];
let app = Promise.resolve({}); // the app's extras, loading
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
      meters.setNames(msg.meters);
      $("#controls").replaceChildren();
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
      if (msg.tempo) tempo.update(msg.tempo, now);
      if (msg.levels) meters.update(msg.levels, now);
      if (msg.cpu) cpu.update(msg.cpu, now);
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
  status.dataset.state = state;
  status.textContent = { live: "Connected", waiting: "Waiting for the Pod…", off: "Not connected" }[state];
  $("#connect").textContent = link.connected ? "Disconnect" : "Connect";
  document.body.classList.toggle("is-live", link.live);
}

$("#connect").addEventListener("click", () => {
  if (link.connected) link.disconnect();
  else link.connect(inputSel.value, outputSel.value);
});
for (const sel of [inputSel, outputSel])
  sel.addEventListener("change", () => link.connected && link.connect(inputSel.value, outputSel.value));

for (const b of document.querySelectorAll("[data-span]"))
  b.addEventListener("click", () => {
    view.spanSeconds = Number(b.dataset.span);
    for (const o of document.querySelectorAll("[data-span]"))
      o.setAttribute("aria-pressed", String(o === b));
  });

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
  showStatus();
}

// ---------------------------------------------------------------- Drawing

function frame() {
  // performance.now(), the clock the frames were stamped with (the
  // callback's own timestamp can lag it).
  const now = performance.now();
  view.draw(now);
  tempo.draw(now);
  meters.draw();
  cpu.draw(now);
  requestAnimationFrame(frame);
}

start();
requestAnimationFrame(frame);

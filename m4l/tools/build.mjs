// Build - a Max for Live device per app, from its parameter list
//
//   node m4l/tools/build.mjs <app>=<dumper> ...
//
// Each dumper is tools/dump_params.cpp built for that app (`make m4l` does
// both): it prints the app's hello and parameter descriptions, which
// web/js/protocol.js decodes. web/js/apps/<app>.js adds the page's menus
// (continuous parameters that pick from a list) and card colours. Writes
// m4l/<app>.amxd and m4l/specs/<app>.json, which daisy_link.mjs reads.
//
// Every parameter becomes a Live parameter (triggers excepted), laid out as
// the page does: a card per group, knobs then choices then switches. Cards
// that don't fit in one device width go in banks, picked down the left.
// Wavebyte's layers: only layer A's parameters get controls, and they edit
// whichever layer the Pod edits (link_core.mjs maps them).

import { execFileSync } from "node:child_process";
import { mkdirSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

import { Kind, decode } from "../../web/js/protocol.js";

const M4L = join(dirname(fileURLToPath(import.meta.url)), "..");
const SKIP_GROUPS = ["Pod Pages"]; // which page the Pod's knobs are on: nothing to do in Live
const SYNTHS = ["wavebyte"]; // MIDI effects, to go before an External Instrument; the rest are audio effects

// Presentation layout, in pixels. A device is 169 high.
const DEVICE_H = 169;
const HEADER_H = 22;
const CARD_Y = HEADER_H + 2;
const CARD_H = DEVICE_H - CARD_Y - 3;
const TITLE_H = 15;
const CONTENT_H = CARD_H - TITLE_H - 4;
const PAD = 4;
const DIAL = { w: 44, h: 48, cellW: 48, cellH: 52 };
const WIDE = { w: 84, cellW: 88 };
const MENU_H = 30; // label + menu
const SWITCH_H = 19;
const BANK_W = 800; // cards per bank, at most
const BANK_COL_W = 70;
const MAX_SINGLE_W = 960; // wider than this, and the cards go in banks

// --- The app's parameters

function readDump(dumper) {
  const bytes = execFileSync(dumper);
  let hello = null;
  const params = [];
  let start = 0;
  for (let i = 0; i < bytes.length; i++) {
    if (bytes[i] !== 0xf7) continue;
    const m = decode(bytes.subarray(start, i + 1));
    start = i + 1;
    if (m?.type === "hello") hello = m;
    else if (m?.type === "params") params.push(...m.params);
  }
  if (!hello || params.length !== hello.numParams)
    throw new Error(`${dumper}: expected a hello and ${hello?.numParams} parameters, got ${params.length}`);
  return { hello, params };
}

async function appModule(app) {
  try {
    return (await import(`../../web/js/apps/${app}.js`)).default;
  } catch {
    return {};
  }
}

function colour(css) {
  let m = /^#([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(css || "");
  if (m) return [1, 2, 3].map((i) => parseInt(m[i], 16) / 255);
  m = /^rgb\((\d+),\s*(\d+),\s*(\d+)\)$/.exec(css || "");
  if (m) return [1, 2, 3].map((i) => parseInt(m[i], 10) / 255);
  return null;
}

// The controls: which parameters, as what, in which cards.
function describe(app, hello, params, mod) {
  // Layers: B's parameters start "B: ", so A's run is the layer's length.
  const firstB = params.findIndex((p) => /^B: /.test(p.name));
  const layerParams = firstB > 0 ? firstB : 0;
  const layers = layerParams ? new Set(params.filter((p) => /^[A-Z]: /.test(p.name)).map((p) => p.name[0])).size + 1 : 0;

  const controls = [];
  const cards = new Map(); // title -> { title, colour, controls }
  const names = new Set(["Device synced with Pod"]); // the device's own hidden flag
  for (const p of params) {
    if (layerParams && p.index >= layerParams && p.index < layerParams * layers) continue;
    const group = hello.groups[p.group] ?? "";
    const title = group.replace(/^[A-Z*]\//, "");
    if (SKIP_GROUPS.includes(title)) continue;

    let kind;
    let options = p.options;
    const menu = mod.menu?.(p.name);
    if (p.kind === Kind.CONTINUOUS) {
      if (Array.isArray(menu)) (kind = "menu"), (options = menu);
      else kind = "dial";
    } else if (p.kind === Kind.CHOICE) {
      kind = options.length <= 4 && options.every((o) => o.length <= 4) ? "tab" : "choice";
    } else if (p.kind === Kind.TOGGLE) kind = "toggle";
    else kind = "trigger";

    let name = p.name;
    if (names.has(name)) name = `${title} ${name}`;
    names.add(name);
    // "Chorus Dry/Wet" in the Chorus card is Dry/Wet; "AUX type" stays.
    const rest = name.startsWith(`${title} `) ? name.slice(title.length + 1) : "";
    const short = /^[A-Z0-9]/.test(rest) ? rest : name;

    const c = {
      id: controls.length,
      index: p.index,
      name,
      podName: p.name,
      short,
      kind,
      options: kind === "dial" || kind === "toggle" || kind === "trigger" ? undefined : options,
      layer: layerParams > 0 && p.index < layerParams,
    };
    if (kind === "menu") c.n = options.length;
    controls.push(c);
    if (!cards.has(title)) cards.set(title, { title, colour: colour(mod.groupColour?.(title)), controls: [] });
    cards.get(title).controls.push(c);
  }

  // Within a card: knobs, then choices, then switches, as on the page.
  const order = { dial: 0, menu: 1, tab: 1, choice: 1, toggle: 2, trigger: 3 };
  for (const card of cards.values()) card.controls.sort((a, b) => order[a.kind] - order[b.kind] || a.id - b.id);

  const spec = {
    app,
    numParams: hello.numParams,
    layerParams,
    layers,
    layerParam: layerParams ? mod.tabParam || "Layer" : null,
    pullOnly: layerParams > 0,
    controls: controls.map(({ id, index, name, podName, kind, n, layer }) => ({ id, index, name, podName, kind, n, layer })),
  };
  return { spec, controls, cards: [...cards.values()] };
}

// --- Layout

// Places a card's controls from (0, 0): knobs in two rows, then columns of
// the rest. Returns its width.
function placeCard(card) {
  const dials = card.controls.filter((c) => c.kind === "dial");
  const rest = card.controls.filter((c) => c.kind !== "dial");
  let x = PAD;
  dials.forEach((c, i) => {
    c.at = { x: x + Math.floor(i / 2) * DIAL.cellW, y: (i % 2) * DIAL.cellH };
  });
  x += Math.ceil(dials.length / 2) * DIAL.cellW;
  let y = 0;
  let column = false;
  for (const c of rest) {
    const h = c.kind === "toggle" || c.kind === "trigger" ? SWITCH_H : MENU_H;
    if (y + h > CONTENT_H && column) (x += WIDE.cellW), (y = 0);
    c.at = { x, y };
    y += h;
    column = true;
  }
  if (column) x += WIDE.cellW;
  card.w = Math.max(x + PAD - 4, card.title.length * 6 + 2 * PAD);
  return card.w;
}

function layout(cards) {
  for (const card of cards) placeCard(card);
  const total = cards.reduce((w, c) => w + c.w + 2, 0);
  const banks = [];
  if (total <= MAX_SINGLE_W) banks.push(cards);
  else {
    let bank = [];
    let w = 0;
    for (const card of cards) {
      if (bank.length && w + card.w > BANK_W) banks.push(bank), (bank = []), (w = 0);
      bank.push(card);
      w += card.w + 2;
    }
    if (bank.length) banks.push(bank);
  }
  const left = banks.length > 1 ? BANK_COL_W : 3;
  let width = 0;
  banks.forEach((bank, b) => {
    let x = left;
    for (const card of bank) {
      card.bank = b;
      card.x = x;
      x += card.w + 2;
    }
    width = Math.max(width, x + 1);
  });
  return { banks, width, left };
}

// --- The patcher

class Patcher {
  constructor() {
    this.boxes = [];
    this.lines = [];
    this.next = 1;
    this.row = 0; // patching view: where the next control's column goes
  }

  add(fields) {
    const id = `obj-${this.next++}`;
    this.boxes.push({ box: { id, ...fields } });
    return id;
  }

  obj(text, rect, numinlets, numoutlets, outlettype = []) {
    return this.add({ maxclass: "newobj", text, patching_rect: rect, numinlets, numoutlets, outlettype });
  }

  connect(from, outlet, to, inlet) {
    this.lines.push({ patchline: { source: [from, outlet], destination: [to, inlet] } });
  }
}

const valueof = (c, fields) => ({
  saved_attribute_attributes: {
    valueof: {
      parameter_longname: c.name,
      parameter_shortname: c.short.slice(0, 20),
      parameter_initial: [0],
      parameter_initial_enable: 1,
      parameter_modmode: 0,
      ...fields,
    },
  },
});

// A control's box, by kind, at its presentation rect.
function controlBox(c, varname, patching, rect) {
  const common = { varname, patching_rect: patching, presentation: 1, presentation_rect: rect, parameter_enable: 1 };
  switch (c.kind) {
    case "dial":
      return {
        maxclass: "live.dial", numinlets: 1, numoutlets: 2, outlettype: ["", "float"], ...common,
        ...valueof(c, { parameter_type: 0, parameter_mmin: 0, parameter_mmax: 100, parameter_unitstyle: 5 }),
      };
    case "menu":
    case "choice":
      return {
        maxclass: "live.menu", numinlets: 1, numoutlets: 3, outlettype: ["", "", "float"], ...common,
        ...valueof(c, { parameter_type: 2, parameter_enum: c.options, parameter_mmax: c.options.length - 1 }),
      };
    case "tab":
      return {
        maxclass: "live.tab", numinlets: 1, numoutlets: 3, outlettype: ["", "", "float"], ...common,
        num_lines_patching: 1, num_lines_presentation: 1,
        ...valueof(c, { parameter_type: 2, parameter_enum: c.options, parameter_mmax: c.options.length - 1 }),
      };
    case "toggle":
      return {
        maxclass: "live.text", numinlets: 1, numoutlets: 2, outlettype: ["", ""], ...common,
        mode: 1, text: c.short, texton: c.short,
        ...valueof(c, { parameter_type: 2, parameter_enum: ["off", "on"], parameter_mmax: 1 }),
      };
    default: // trigger: not a Live parameter
      return {
        maxclass: "live.text", numinlets: 1, numoutlets: 2, outlettype: ["", ""], ...common,
        parameter_enable: 0, mode: 0, text: c.short, texton: c.short,
      };
  }
}

const comment = (text, varname, patching, rect, extra = {}) => ({
  maxclass: "live.comment", numinlets: 1, numoutlets: 0, text, ...(varname ? { varname } : {}), patching_rect: patching,
  presentation: 1, presentation_rect: rect, fontsize: 9.5, textjustification: 0, ...extra,
});

// A header or bank button, outside Live's parameters.
const button = (text, varname, toggle, patching, rect) => ({
  maxclass: "live.text", numinlets: 1, numoutlets: 2, outlettype: ["", ""], text, texton: text,
  varname, mode: toggle ? 1 : 0, parameter_enable: 0, patching_rect: patching, presentation: 1,
  presentation_rect: rect, fontsize: 9.0,
});

function buildPatcher(app, kind, cards, { banks, width, left }) {
  const P = new Patcher();

  // The engine room, top left of the patching view.
  const hub = P.obj("v8 daisy_hub.js", [20, 60, 110, 22], 1, 1, [""]);
  const node = P.obj(`node.script daisy_link.mjs @autostart 1 @watch 0 @args ${app}`, [20, 110, 320, 22], 1, 2, ["", ""]);
  P.connect(hub, 0, node, 0);
  P.connect(node, 0, hub, 0);
  if (kind === "midi") {
    const i = P.obj("midiin", [380, 20, 50, 22], 1, 1, ["int"]);
    const o = P.obj("midiout", [380, 60, 55, 22], 1, 0);
    P.connect(i, 0, o, 0);
  } else {
    const i = P.obj("plugin~", [380, 20, 53, 22], 2, 2, ["signal", "signal"]);
    const o = P.obj("plugout~", [380, 60, 53, 22], 2, 2, ["signal", "signal"]);
    P.connect(i, 0, o, 0);
    P.connect(i, 1, o, 1);
  }

  // Wires something's outlet 0 into the hub through a [prepend].
  let px = 20;
  let py = 180;
  const toHub = (from, prefix) => {
    const p = P.obj(`prepend ${prefix}`, [px, py + 60, 90, 22], 1, 1, [""]);
    P.connect(from, 0, p, 0);
    P.connect(p, 0, hub, 0);
  };
  const slot = () => {
    const at = [px, py, 90, 50];
    px += 110;
    if (px > 1500) (px = 20), (py += 120);
    return at;
  };

  // Header: status, presets, Get from Pod.
  let hx = left;
  P.add(comment("Starting", "status", slot(), [hx, 3, 190, 18]));
  hx += 194;
  P.add(comment("Presets", null, slot(), [hx, 3, 42, 18]));
  hx += 42;
  for (let s = 0; s < 8; s++) {
    toHub(P.add(button(`${s + 1}`, `preset${s}`, true, slot(), [hx, 4, 15, 15])), `presetclick ${s}`);
    hx += 17;
  }
  toHub(P.add(button("Save", "save", false, slot(), [hx + 2, 4, 32, 15])), "save");
  hx += 36;
  P.add(comment("", "presetnote", slot(), [hx, 3, 38, 18]));
  hx += 40;
  toHub(P.add(button("Get from Pod", "pull", false, slot(), [hx, 4, 68, 15])), "pull");
  hx += 72;

  // Whether this device has synced before, kept with the set.
  const synced = P.add({
    maxclass: "live.toggle", numinlets: 1, numoutlets: 1, outlettype: [""], varname: "synced",
    parameter_enable: 1, patching_rect: slot(),
    ...valueof({ name: "Device synced with Pod", short: "Device synced" }, { parameter_type: 2, parameter_enum: ["off", "on"], parameter_mmax: 1, parameter_invisible: 1 }),
  });
  toHub(synced, "stored_synced");

  // Bank buttons, down the left.
  if (banks.length > 1)
    banks.forEach((bank, b) => {
      const id = P.add(button(bank[0].title, `bank${b}`, true, slot(), [3, CARD_Y + b * 17, BANK_COL_W - 8, 15]));
      toHub(id, `bank ${b}`);
    });

  // The cards.
  for (const card of cards) {
    const b = `b${card.bank}_`;
    const tint = card.colour || [0.5, 0.5, 0.5];
    P.add({
      maxclass: "panel", numinlets: 1, numoutlets: 0, varname: `${b}panel${card.x}`, background: 1,
      patching_rect: [px, 20, 20, 20], presentation: 1, presentation_rect: [card.x, CARD_Y, card.w, CARD_H],
      bgcolor: [...tint, 0.13], border: 0, rounded: 4, mode: 0,
    });
    P.add(comment(card.title, `${b}title${card.x}`, [px, 44, 60, 18], [card.x + 2, CARD_Y, card.w - 4, TITLE_H],
      card.colour ? { textcolor: [...card.colour, 1] } : {}));
    const top = CARD_Y + TITLE_H + 2;
    for (const c of card.controls) {
      const x = card.x + c.at.x;
      const y = top + c.at.y;
      let rect;
      if (c.kind === "dial") rect = [x, y, DIAL.w, DIAL.h];
      else if (c.kind === "toggle" || c.kind === "trigger") rect = [x, y, WIDE.w, 16];
      else {
        P.add(comment(c.short, `${b}label${c.id}`, [px, py + 30, 80, 18], [x - 2, y - 2, WIDE.w + 4, 14]));
        rect = [x, y + 12, WIDE.w, 15];
      }
      toHub(P.add(controlBox(c, `${b}c${c.id}`, slot(), rect)), `${c.id}`);
    }
  }

  return {
    patcher: {
      fileversion: 1,
      appversion: { major: 9, minor: 1, revision: 5, architecture: "x64", modernui: 1 },
      classnamespace: "box",
      rect: [100, 100, 1640, 900],
      openrect: [0, 0, 0, DEVICE_H],
      bglocked: 0,
      openinpresentation: 1,
      default_fontsize: 10,
      default_fontface: 0,
      default_fontname: "Arial Bold",
      gridonopen: 1,
      gridsize: [8, 8],
      gridsnaponopen: 1,
      objectsnaponopen: 1,
      statusbarvisible: 2,
      toolbarvisible: 1,
      lefttoolbarpinned: 0,
      toptoolbarpinned: 0,
      righttoolbarpinned: 0,
      bottomtoolbarpinned: 0,
      toolbars_unpinned_last_save: 0,
      tallnewobj: 0,
      boxanimatetime: 500,
      enablehscroll: 1,
      enablevscroll: 1,
      devicewidth: Math.max(width, hx + 4),
      description: `Daisy Pod ${app}: every parameter, synced both ways over USB`,
      digest: "",
      tags: "",
      style: "",
      subpatcher_template: "",
      boxes: P.boxes,
      lines: P.lines,
      dependency_cache: [],
      latency: 0,
      project: {
        version: 1,
        creationdate: 3590052493,
        modificationdate: 3590052493,
        viewrect: [0, 0, 300, 500],
        autoorganize: 1,
        hideprojectwindow: 1,
        showdependencies: 1,
        autolocalize: 0,
        contents: { patchers: {} },
        layout: {},
        searchpath: {},
        detailsvisible: 0,
        amxdtype: kind === "midi" ? 1835887981 : 1633771873,
        readonly: 0,
        devpathtype: 0,
        devpath: ".",
        sortmode: 0,
        viewmode: 0,
      },
      autosave: 0,
    },
  };
}

// --- The .amxd: "ampf" with the device type, "meta", then "ptch" with the
// patcher as JSON, NUL-terminated.

function chunk(tag, body) {
  const head = Buffer.alloc(8);
  head.write(tag, 0, "ascii");
  head.writeUInt32LE(body.length, 4);
  return Buffer.concat([head, body]);
}

function amxd(kind, patcher) {
  const json = Buffer.from(JSON.stringify(patcher, null, "\t") + "\n\0", "utf8");
  return Buffer.concat([
    chunk("ampf", Buffer.from(kind === "midi" ? "mmmm" : "aaaa", "ascii")),
    chunk("meta", Buffer.alloc(4)),
    chunk("ptch", json),
  ]);
}

// --- Main

const pairs = process.argv.slice(2).map((a) => a.split("="));
if (!pairs.length || pairs.some((p) => p.length !== 2)) {
  console.error("usage: node m4l/tools/build.mjs <app>=<dumper> ...");
  process.exit(1);
}
mkdirSync(join(M4L, "specs"), { recursive: true });
for (const [app, dumper] of pairs) {
  const { hello, params } = readDump(dumper);
  const mod = await appModule(app);
  const { spec, controls, cards } = describe(app, hello, params, mod);
  const placed = layout(cards);
  const kind = SYNTHS.includes(app) ? "midi" : "audio";
  const patcher = buildPatcher(app, kind, cards, placed);
  writeFileSync(join(M4L, `${app}.amxd`), amxd(kind, patcher));
  writeFileSync(join(M4L, "specs", `${app}.json`), JSON.stringify(spec, null, 2) + "\n");
  const params_ = controls.filter((c) => c.kind !== "trigger").length;
  console.log(
    `${app}.amxd: ${controls.length} controls (${params_} Live parameters), ${placed.banks.length} bank(s), ${patcher.patcher.devicewidth} px wide`
  );
}

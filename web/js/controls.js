// The control surface, built from the parameter list the app sends and
// laid out as a faceplate (web/CLAUDE.md has the whole design):
//
// - The feature: the app's main choice (extras.feature, or a group that is
//   one choice of more than 8 options) as tiles beside the display.
//   Options like "Mosaic A" ... "Warp D" split into the names as tiles and
//   the variations as a switch. With no feature, the knob pages go there.
// - The pages ("Page N" groups) as a row of modules, each titled by its
//   knobs with a chip for its number.
// - The strip: small groups (no choices, at most one knob) in one row.
// - The drawer: the groups the app names in extras.drawer, a tab at a time.
// - Other groups: modules in the row after the pages.
// - Sections (extras.sections, wavebyte): a rail beside the display picks
//   one section of cards at a time, instead of all of the above.
//
// Inside a module, always: knobs, then menus, then choices, then switches.
// A menu is a continuous parameter that picks from a list (the app's
// menu(name) gives it), as a knob stepping through it does on the Pod; a
// choice can show as a drop-down too (menu(name) returning true).
//
// Groups named "<tab>/<card>" (wavebyte's layers, "A/Filter") belong to a
// tab, one tab's cards showing at a time; "*/<card>" shows with every tab.
// Their parameters may start with the tab ("B: Cutoff"), which their
// labels leave out. The app's tabParam (a choice whose options are the
// tabs) is the tab switch, in the header.
//
// Changes go out through onSet(index, raw); the app's state comes back in
// through state().

import { createKnob } from "./knob.js";
import { Kind, UNIT_MAX } from "./protocol.js";

const SEND_MS = 25; // at most this often per knob while it's turning
const STORE = "daisy-pod-web:";
const TABBED = /^([^/]+)\/(.+)$/;
const PAGE = /^Page (\d+)$/;
const ENVELOPE = /^(.+) (Attack|Decay|Sustain|Release)$/;
const SLOT = /^(.+?) (\d+) (.+)$/;

// Splits options like "Mosaic A" ... "Warp D" into rows that share a name,
// if they all fit that pattern; otherwise null.
function matrixRows(options, columns) {
  if (columns < 2 || options.length < 2 * columns || options.length % columns) return null;
  const rows = [];
  for (let i = 0; i < options.length; i += columns) {
    const parts = options.slice(i, i + columns).map((o) => {
      const at = o.lastIndexOf(" ");
      return at > 0 ? [o.slice(0, at), o.slice(at + 1)] : null;
    });
    if (parts.some((p) => !p || p[0] !== parts[0][0])) return null;
    rows.push({ name: parts[0][0], cells: parts.map((p, j) => ({ label: p[1], index: i + j })) });
  }
  return rows;
}

function throttled(send) {
  let last = 0;
  let pending = null;
  let timer = 0;
  return (raw) => {
    pending = raw;
    const wait = last + SEND_MS - performance.now();
    if (wait <= 0) {
      last = performance.now();
      send(pending);
      pending = null;
    } else if (!timer) {
      timer = setTimeout(() => {
        timer = 0;
        last = performance.now();
        if (pending !== null) send(pending);
        pending = null;
      }, wait);
    }
  };
}

const el = (tag, className, text) => {
  const e = document.createElement(tag);
  if (className) e.className = className;
  if (text !== undefined) e.textContent = text;
  return e;
};

function remember(key, fallback) {
  try {
    const v = localStorage.getItem(STORE + key);
    return v === null ? fallback : Number(v);
  } catch {
    return fallback;
  }
}

function store(key, value) {
  try {
    localStorage.setItem(STORE + key, String(value));
  } catch {}
}

// Dark or light text on a colour: by its luminance (hex or rgb()).
function inkOn(colour) {
  let r, g, b;
  const hex = /^#([0-9a-f]{6})$/i.exec(colour || "");
  const rgb = /rgb\(\s*(\d+)[ ,]+(\d+)[ ,]+(\d+)/.exec(colour || "");
  if (hex) [r, g, b] = [0, 2, 4].map((i) => parseInt(hex[1].slice(i, i + 2), 16));
  else if (rgb) [r, g, b] = rgb.slice(1).map(Number);
  else return "";
  const lum = (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255;
  return lum > 0.55 ? "#15181a" : "#ffffff";
}

function setAccent(node, colour) {
  if (!colour) return;
  node.style.setProperty("--accent", colour);
  node.style.setProperty("--chip-ink", inkOn(colour));
}

// A choice as a segmented control. An option with its own colour shows it
// as a dot.
function segmented(param, app, onPick, columns) {
  const root = el("div", "segmented");
  root.style.setProperty("--columns", String(columns || param.columns || param.options.length));
  root.setAttribute("role", "group");
  root.setAttribute("aria-label", param.name);
  const buttons = param.options.map((label, i) => {
    const b = el("button");
    b.type = "button";
    b.append(el("span", "seg-text", label));
    b.setAttribute("aria-pressed", "false");
    const colour = app.optionColour?.(param.name, i, param.options[i]);
    if (colour) {
      b.classList.add("dot");
      b.style.setProperty("--option", colour);
    }
    b.addEventListener("click", () => onPick(i));
    root.append(b);
    return b;
  });
  const select = (index) => buttons.forEach((b, i) => b.setAttribute("aria-pressed", String(i === index)));
  return { el: root, select };
}

// A switch: a toggle with a light showing its state, or a trigger with a ring.
function switchButton(kind, label, app, name = label) {
  const b = el("button", kind);
  b.type = "button";
  b.append(el("span", "led"), el("span", "", label));
  const led = app.ledColour?.(name);
  if (led) b.style.setProperty("--led", led);
  return b;
}

// The envelope's shape, from its four knobs (0..1 each).
function envelopePath(v) {
  const ax = 10 + v.Attack * 60;
  const dx = ax + 10 + v.Decay * 60;
  const sx = dx + 70;
  const rx = Math.min(298, sx + 10 + v.Release * 70);
  const sy = 76 - v.Sustain * 66;
  return `M10 76L${ax.toFixed(1)} 10L${dx.toFixed(1)} ${sy.toFixed(1)}L${sx.toFixed(1)} ${sy.toFixed(1)}L${rx.toFixed(1)} 76Z`;
}

export class Controls {
  constructor({ surface, feature, below, rail, headerExtra }, onSet) {
    this.surface = surface;
    this.feature = feature;
    this.below = below;
    this.rail = rail;
    this.headerExtra = headerExtra;
    this.onSet = onSet;
    this.widgets = [];
    this.tabs = [];
    this.tab = 0;
    this.tabSwitch = null;
    this.overrides = []; // { el: the choice taken over, by: the index of the one taking it }
    this.visibility = []; // { el, name }: cards the app shows only sometimes
    this.sections = []; // { button, el, scope, note }
    this.values = [];
  }

  build(hello, params, app) {
    for (const zone of [this.feature, this.below, this.rail, this.headerExtra]) zone.replaceChildren();
    this.widgets = [];
    this.overrides = [];
    this.visibility = [];
    this.sections = [];
    this.app = app;
    this.appName = hello.app;
    this.byName = new Map(params.map((p) => [p.name, p.index]));

    // Tabs, from groups named "<tab>/<card>".
    this.tabs = [];
    for (const name of hello.groups) {
      const m = TABBED.exec(name);
      if (m && m[1] !== "*" && !this.tabs.includes(m[1])) this.tabs.push(m[1]);
    }
    const tabParam = this.tabs.length && app.tabParam ? params.find((p) => p.name === app.tabParam) : undefined;
    if (this.tabs.length) this.headerExtra.append(this.buildTabSwitch(tabParam, app));

    // The feature: the app's main choice.
    const featureParam = app.feature
      ? params.find((p) => p.name === app.feature)
      : params.find((p) => p.kind === Kind.CHOICE && p.options.length > 8 && params.filter((q) => q.group === p.group).length === 1);

    const groups = hello.groups
      .map((fullName, g) => {
        const tabbed = TABBED.exec(fullName);
        const tab = tabbed && tabbed[1] !== "*" ? tabbed[1] : undefined;
        const name = tabbed ? tabbed[2] : fullName;
        const members = params.filter((p) => p.group === g && p !== tabParam && p !== featureParam);
        const nameOf = (p) => (tab && p.name.startsWith(`${tab}: `) ? p.name.slice(tab.length + 2) : p.name);
        return { name, tab, everyTab: tabbed?.[1] === "*", members, nameOf, page: PAGE.exec(name)?.[1] };
      })
      .filter((g) => g.members.length);

    const withRail = Boolean(app.sections);
    this.surface.classList.toggle("with-rail", withRail);
    this.rail.hidden = !withRail;
    if (withRail) this.buildSections(groups, app);
    else this.buildPlate(groups, app, featureParam, hello);
    if (this.tabs.length) this.showTab(this.tab);
  }

  // ---------------------------------------------------------------- The plate

  buildPlate(groups, app, featureParam, hello) {
    if (featureParam) {
      const home = hello.groups[featureParam.group];
      this.feature.append(this.buildFeature(featureParam, app, TABBED.exec(home)?.[2] ?? home));
    }
    const pages = el("div", "pages");
    const modules = el("div", "pages");
    const strip = el("section", "strip");
    strip.setAttribute("aria-label", "Switches");
    const drawerNames = app.drawer || [];
    const drawerGroups = [];
    for (const g of groups) {
      if (drawerNames.includes(g.name)) drawerGroups.push(g);
      else if (g.page) pages.append(this.module(g, app));
      else if (this.isSmall(g, app)) strip.append(this.stripGroup(g, app));
      else (featureParam ? pages : modules).append(this.module(g, app));
    }
    // Without a feature, the pages sit beside the display.
    (featureParam ? this.below : this.feature).append(pages);
    if (modules.childElementCount) this.below.append(modules);
    if (strip.childElementCount) (featureParam ? this.below : this.feature).append(strip);
    if (drawerGroups.length) {
      drawerGroups.sort((a, b) => drawerNames.indexOf(a.name) - drawerNames.indexOf(b.name));
      this.below.append(this.drawer(drawerGroups, app));
    }
    for (const zone of [pages, modules]) if (!zone.childElementCount) zone.remove();
  }

  // No choices or menus, and at most one knob: fits in the strip.
  isSmall(g, app) {
    const knobs = g.members.filter((p) => p.kind === Kind.CONTINUOUS);
    return !g.members.some((p) => p.kind === Kind.CHOICE) && knobs.length <= 1 && !knobs.some((p) => app.menu?.(g.nameOf(p)));
  }

  // The main choice as tiles. Options like "Mosaic A" split into the names
  // and the variations.
  buildFeature(p, app, home) {
    const card = el("section", "module feature-module");
    card.setAttribute("aria-label", p.name);
    const head = el("div", "module-head");
    const value = el("span", "module-value");
    head.append(el("h2", "", p.options.length > 8 && home !== p.name ? home : p.name), value);
    card.append(head);
    const send = (raw) => this.onSet(p.index, raw);
    const rows = matrixRows(p.options, p.columns);

    if (rows) {
      // The names as tiles, the variations as a switch: picking one keeps the other.
      const n = p.columns;
      let current = 0;
      const tiles = el("div", "tiles");
      tiles.style.setProperty("--tile-min", "96px");
      tiles.setAttribute("role", "group");
      tiles.setAttribute("aria-label", p.name);
      const names = rows.map((row, r) => {
        const b = el("button", "tile");
        b.type = "button";
        b.append(el("span", "tile-name", row.name));
        b.addEventListener("click", () => pick(r * n + (current % n), true));
        tiles.append(b);
        return b;
      });
      const variationParam = { name: "Variation", options: rows[0].cells.map((c) => c.label), columns: n };
      const variations = segmented(
        { ...variationParam, name: p.name },
        { optionColour: (_, i) => app.optionColour?.(p.name, i, p.options[i]) },
        (v) => pick(Math.floor(current / n) * n + v, true),
        n,
      );
      variations.el.setAttribute("aria-label", "Variation");
      const variation = el("div", "variation");
      variation.append(el("span", "label", "Variation"), variations.el);
      card.append(tiles, variation);
      const pick = (raw, user) => {
        current = raw;
        names.forEach((b, r) => b.setAttribute("aria-pressed", String(r === Math.floor(raw / n))));
        variations.select(raw % n);
        value.textContent = p.options[raw] ?? "";
        if (user) send(raw);
      };
      this.widgets[p.index] = { set: (raw) => pick(raw, false) };
      return card;
    }

    // Tiles, big ones for a few options, each with the app's hint.
    const tiles = el("div", p.options.length <= 6 ? "tiles big" : "tiles");
    tiles.setAttribute("role", "group");
    tiles.setAttribute("aria-label", p.name);
    const buttons = p.options.map((label, i) => {
      const b = el("button", "tile");
      b.type = "button";
      b.setAttribute("aria-pressed", "false");
      b.append(el("span", "tile-name", label));
      const hint = app.optionHint?.(p.name, i);
      if (hint) b.append(el("span", "tile-hint", hint));
      const colour = app.optionColour?.(p.name, i, label);
      if (colour) {
        b.classList.add("dot");
        b.style.setProperty("--option", colour);
      }
      b.addEventListener("click", () => {
        pick(i);
        send(i);
      });
      tiles.append(b);
      return b;
    });
    card.append(tiles);
    const pick = (i) => {
      buttons.forEach((b, j) => b.setAttribute("aria-pressed", String(j === i)));
      value.textContent = p.options[i] ?? "";
    };
    this.widgets[p.index] = { set: pick };
    return card;
  }

  stripGroup(g, app) {
    const box = el("div", "strip-group");
    box.setAttribute("role", "group");
    box.setAttribute("aria-label", g.name);
    const parts = { knobs: el("div"), menus: el("div"), choices: el("div"), switches: el("div") };
    for (const p of g.members) this.widgets[p.index] = this.widget(p, app, parts, { name: g.nameOf(p) });
    // A knob named as its group (grainfall's Tempo) needn't say so twice.
    for (const label of parts.knobs.querySelectorAll(".knob-label")) if (label.textContent === g.name) label.hidden = true;
    box.append(el("h2", "", app.groupTitle?.(g.name, g.members.map(g.nameOf)) || g.name), ...Object.values(parts).filter((x) => x.childElementCount));
    return box;
  }

  // The rest, a tab at a time.
  drawer(groups, app) {
    const box = el("section", "drawer");
    box.setAttribute("aria-label", "Settings");
    const tabs = el("div", "drawer-tabs");
    tabs.setAttribute("role", "tablist");
    tabs.setAttribute("aria-label", "Settings");
    box.append(tabs);
    const key = `drawer:${this.appName}`;
    let open = Math.min(remember(key, 0), groups.length - 1);
    const panels = groups.map((g, i) => {
      const id = `drawer-${this.appName}-${i}`;
      const tab = el("button", "", app.groupTitle?.(g.name, g.members.map(g.nameOf)) || g.name);
      tab.type = "button";
      tab.id = `${id}-tab`;
      tab.setAttribute("role", "tab");
      tab.setAttribute("aria-controls", id);
      tabs.append(tab);
      const panel = el("div", "drawer-panel");
      panel.id = id;
      panel.setAttribute("role", "tabpanel");
      panel.setAttribute("aria-labelledby", tab.id);
      panel.append(...this.parts(g, app));
      box.append(panel);
      tab.addEventListener("click", () => show(i));
      return { tab, panel };
    });
    const show = (i) => {
      open = i;
      store(key, i);
      panels.forEach((p, j) => {
        p.tab.setAttribute("aria-selected", String(j === i));
        p.tab.tabIndex = j === i ? 0 : -1;
        p.panel.hidden = j !== i;
      });
    };
    tabs.addEventListener("keydown", (e) => {
      const step = { ArrowRight: 1, ArrowLeft: -1 }[e.key];
      if (!step) return;
      e.preventDefault();
      show((open + step + panels.length) % panels.length);
      panels[open].tab.focus();
    });
    show(open);
    return box;
  }

  // ---------------------------------------------------------------- Sections (wavebyte)

  buildSections(groups, app) {
    const defs = app.sections.map((s) => ({ ...s, cards: [] }));
    let more = null;
    for (const g of groups) {
      let def = defs.find((s) => s.groups.includes(g.name));
      if (!def) def = more ??= { label: "More", groups: [], cards: [] };
      def.cards.push(g);
    }
    if (more) defs.push(more);

    const key = `section:${this.appName}`;
    let open = Math.min(remember(key, 0), defs.length - 1);
    this.sections = defs.map((def, i) => {
      const button = el("button");
      button.type = "button";
      const chips = el("span", "rail-chips");
      chips.setAttribute("aria-hidden", "true");
      const colours = [...new Set(def.cards.map((g) => app.groupColour?.(g.name)).filter(Boolean))].slice(0, 3);
      for (const c of colours.length ? colours : ["#8f969c"]) {
        const chip = el("span");
        chip.style.background = c;
        chips.append(chip);
      }
      const layered = def.cards.some((g) => g.tab);
      const scope = el("span", "rail-scope");
      button.append(chips, el("span", "", def.label), scope);
      button.addEventListener("click", () => show(i));
      this.rail.append(button);

      const section = el("section", "section");
      section.setAttribute("aria-label", def.label);
      const head = el("div", "section-head");
      const note = el("span", "section-note");
      head.append(el("h2", "", def.label), note);
      const cards = el("div", "section-cards");
      for (const g of def.cards) {
        const card = this.module(g, app, "h3");
        if (g.tab) card.dataset.tab = g.tab;
        if (app.groupVisible) this.visibility.push({ el: card, name: g.name });
        cards.append(card);
      }
      section.append(head, cards);
      this.feature.append(section);
      return { button, el: section, scope, note, layered, def };
    });
    const show = (i) => {
      open = i;
      store(key, i);
      this.sections.forEach((s, j) => {
        s.el.hidden = j !== i;
        s.button.setAttribute("aria-current", String(j === i));
      });
    };
    show(open);
    this.labelSections();
  }

  // Which layer each section edits, in the rail and the section's head.
  labelSections() {
    const tab = this.tabs[this.tab];
    for (const s of this.sections) {
      s.scope.textContent = s.layered ? tab ?? "" : "all";
      s.note.textContent = [s.layered && tab ? `Layer ${tab}` : "Shared by all layers", s.def.note].filter(Boolean).join(" · ");
    }
  }

  // ---------------------------------------------------------------- Tabs

  // The tab switch, in the header, as the tab parameter's control.
  buildTabSwitch(tabParam, app) {
    const box = el("div", "header-tabs");
    const name = tabParam?.name ?? "Tab";
    const param = { name, options: this.tabs, columns: this.tabs.length };
    const opts = segmented(param, app, (i) => {
      this.showTab(i);
      if (tabParam) this.onSet(tabParam.index, i);
    });
    opts.el.style.minWidth = `${this.tabs.length * 56}px`;
    this.tabSwitch = opts;
    box.append(el("span", "label", name), opts.el);
    if (tabParam) this.widgets[tabParam.index] = { set: (raw) => this.showTab(raw) };
    this.tab = Math.min(this.tab, this.tabs.length - 1);
    return box;
  }

  showTab(i) {
    if (i < 0 || i >= this.tabs.length) return;
    this.tab = i;
    this.tabSwitch?.select(i);
    for (const card of this.feature.querySelectorAll("[data-tab]")) card.hidden = card.dataset.tab !== this.tabs[i] || card.dataset.off === "true";
    this.labelSections();
  }

  // ---------------------------------------------------------------- Modules

  module(g, app, heading = "h2") {
    const card = el("section", "module");
    const title = app.groupTitle?.(g.name, g.members.map(g.nameOf)) || this.title(g);
    setAccent(card, app.groupColour?.(g.name));
    const head = el("div", "module-head");
    if (g.page) {
      const chip = el("span", "chip", g.page);
      chip.setAttribute("aria-label", g.name);
      head.append(chip);
    }
    head.append(el(heading, "", title));
    card.setAttribute("aria-label", g.page ? `${g.name}: ${title}` : title);
    card.append(head, ...this.parts(g, app, title));
    return card;
  }

  // A page is titled by its knobs (the chip has its number), or by what's
  // on it if it has none.
  title(g) {
    if (!g.page) return g.name;
    const knobs = g.members.filter((m) => m.kind === Kind.CONTINUOUS).map(g.nameOf);
    return (knobs.length ? knobs : g.members.map(g.nameOf)).join(" · ");
  }

  // A module's contents, in order: envelopes and slots if it has them, then
  // knobs, menus, choices and switches.
  parts(g, app, title = g.name) {
    const out = [];
    let rest = g.members;

    // Envelopes: four knobs named "<x> Attack" ... "<x> Release".
    const envs = new Map();
    for (const p of rest) {
      const m = ENVELOPE.exec(g.nameOf(p));
      if (m) envs.set(m[1], { ...(envs.get(m[1]) || {}), [m[2]]: p });
    }
    const full = [...envs].filter(([, e]) => e.Attack && e.Decay && e.Sustain && e.Release);
    if (full.length) {
      const box = el("div", "envelopes");
      for (const [name, e] of full) box.append(this.envelope(name, e, app));
      out.push(box);
      const used = new Set(full.flatMap(([, e]) => Object.values(e)));
      rest = rest.filter((p) => !used.has(p));
    }

    // Slots: "<x> <n> <field>" for three or more n (wavebyte's mod matrix;
    // its two LFOs stay knobs and menus).
    const slots = new Map();
    const fields = [];
    for (const p of rest) {
      const m = SLOT.exec(g.nameOf(p));
      if (!m) continue;
      if (!slots.has(m[2])) slots.set(m[2], {});
      slots.get(m[2])[m[3]] = p;
      if (!fields.includes(m[3])) fields.push(m[3]);
    }
    if (slots.size >= 3 && fields.length >= 2 && [...slots.values()].every((s) => fields.every((f) => s[f]))) {
      out.push(this.slotTable(slots, fields, g, app));
      const used = new Set([...slots.values()].flatMap((s) => Object.values(s)));
      rest = rest.filter((p) => !used.has(p));
    }

    const parts = {
      knobs: el("div", "part-knobs"),
      menus: el("div", "part-menus"),
      choices: el("div", "part-choices"),
      switches: el("div", "part-switches"),
    };
    const short = this.shortener(g, rest);
    for (const p of rest) {
      const name = g.nameOf(p);
      this.widgets[p.index] = this.widget(p, app, parts, { name, label: short(name), showLabel: name !== g.name && name !== title });
    }
    for (const p of rest) {
      const over = app.overrides?.(g.nameOf(p));
      const target = over && rest.find((m) => g.nameOf(m) === over);
      if (target) this.overrides.push({ el: this.widgets[target.index]?.el, by: p.index });
    }
    return [...out, ...Object.values(parts).filter((part) => part.childElementCount)];
  }

  // Labels without a first word every member shares and the group's name
  // already gives ("Chorus Dry/Wet" in Chorus, "Compr Ratio" in
  // Compressor), unless what's left would start with a number.
  shortener(g, members) {
    const first = members.length > 1 ? g.nameOf(members[0]).split(" ")[0] : "";
    const shared = first && g.name.startsWith(first) && members.every((p) => g.nameOf(p).startsWith(`${first} `) && !/^\d/.test(g.nameOf(p).slice(first.length + 1)));
    return (name) => (shared ? name.slice(first.length + 1) : name);
  }

  envelope(name, e, app) {
    const box = el("div", "envelope");
    box.append(el("h3", "", name));
    const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    svg.setAttribute("viewBox", "0 0 300 86");
    svg.setAttribute("preserveAspectRatio", "none");
    svg.setAttribute("aria-hidden", "true");
    const path = document.createElementNS("http://www.w3.org/2000/svg", "path");
    path.setAttribute("class", "shape");
    svg.append(path);
    box.append(svg);
    const values = { Attack: 0, Decay: 0, Sustain: 0, Release: 0 };
    const redraw = () => path.setAttribute("d", envelopePath(values));
    const knobs = el("div", "part-knobs");
    for (const stage of ["Attack", "Decay", "Sustain", "Release"]) {
      const p = e[stage];
      const out = throttled((raw) => this.onSet(p.index, raw));
      const knob = createKnob({
        label: stage,
        format: app.format?.(`${name} ${stage}`),
        onInput: (v) => {
          values[stage] = v;
          redraw();
          out(v * UNIT_MAX);
        },
      });
      knobs.append(knob.el);
      this.widgets[p.index] = {
        set: (raw) => {
          knob.set(raw / UNIT_MAX);
          values[stage] = raw / UNIT_MAX;
          redraw();
        },
      };
    }
    redraw();
    box.append(knobs);
    return box;
  }

  // Slots as a table: a row per slot, a column per field. Fields with a
  // list are drop-downs; others are sliders (bipolar ones fill from the
  // middle).
  slotTable(slots, fields, g, app) {
    const scroll = el("div", "slots-scroll");
    const table = el("table", "slots");
    const head = el("tr");
    head.append(el("th", "", "Slot"));
    for (const f of fields) head.append(el("th", "", f));
    const thead = el("thead");
    thead.append(head);
    const tbody = el("tbody");
    table.append(thead, tbody);
    scroll.append(table);
    for (const [n, slot] of slots) {
      const row = el("tr");
      const th = el("th", "", n);
      th.scope = "row";
      row.append(th);
      fields.forEach((f, fi) => {
        const p = slot[f];
        const name = g.nameOf(p);
        const td = el("td");
        const list = app.menu?.(name);
        if (list && list !== true) {
          const select = el("select");
          select.setAttribute("aria-label", name);
          list.forEach((item, i) => select.append(new Option(item, String(i))));
          select.addEventListener("change", () => this.onSet(p.index, Math.round(((Number(select.value) + 0.5) / list.length) * UNIT_MAX)));
          td.append(select);
          const count = list.length;
          this.widgets[p.index] = {
            set: (raw) => {
              select.value = String(Math.min(count - 1, Math.floor((raw / UNIT_MAX) * count)));
              // A slot whose first field is off reads as unused.
              if (fi === 0) row.classList.toggle("off", select.value === "0");
            },
          };
        } else {
          const box = el("div", "amount");
          const input = el("input");
          input.type = "range";
          input.min = "0";
          input.max = String(UNIT_MAX);
          input.setAttribute("aria-label", name);
          const out = el("output");
          const format = app.format?.(name) || ((v) => `${Math.round(v * 100)}%`);
          const bipolar = app.bipolar?.(name);
          const show = (raw) => {
            const v = raw / UNIT_MAX;
            const at = `${(v * 100).toFixed(1)}%`;
            input.style.setProperty("--fill-from", bipolar ? (v < 0.5 ? at : "50%") : "0%");
            input.style.setProperty("--fill-to", bipolar ? (v < 0.5 ? "50%" : at) : at);
            out.textContent = format(v);
            input.setAttribute("aria-valuetext", out.textContent);
          };
          const send = throttled((raw) => this.onSet(p.index, raw));
          input.addEventListener("input", () => {
            show(Number(input.value));
            send(Number(input.value));
          });
          box.append(input, out);
          td.append(box);
          this.widgets[p.index] = {
            set: (raw) => {
              if (document.activeElement === input) return;
              input.value = String(raw);
              show(raw);
            },
          };
        }
        row.append(td);
      });
      tbody.append(row);
    }
    return scroll;
  }

  // name looks up the app's formats, menus and colours; label is shown.
  widget(p, app, parts, { name = p.name, label = name, showLabel = true } = {}) {
    const send = (raw) => this.onSet(p.index, raw);
    switch (p.kind) {
      case Kind.CONTINUOUS: {
        const list = app.menu?.(name);
        if (list) return this.menu(list, parts, send, label);
        const out = throttled(send);
        const knob = createKnob({
          label,
          format: app.format?.(name),
          onInput: (v) => out(v * UNIT_MAX),
        });
        parts.knobs.append(knob.el);
        return { set: (raw) => knob.set(raw / UNIT_MAX) };
      }
      case Kind.CHOICE: {
        if (app.menu?.(name) === true) return this.choiceMenu(p, parts, send, label);
        const box = el("div", "choice");
        if (showLabel) box.append(el("div", "choice-label", label));
        const opts = segmented({ ...p, name }, app, (i) => {
          opts.select(i);
          send(i);
        });
        box.append(opts.el);
        parts.choices.append(box);
        return { set: (raw) => opts.select(raw), el: box };
      }
      case Kind.TOGGLE: {
        const b = switchButton("toggle", label, app, name);
        b.setAttribute("aria-pressed", "false");
        b.addEventListener("click", () => {
          const on = b.getAttribute("aria-pressed") !== "true";
          b.setAttribute("aria-pressed", String(on));
          send(on ? 1 : 0);
        });
        parts.switches.append(b);
        return { set: (raw) => b.setAttribute("aria-pressed", String(raw !== 0)) };
      }
      default: {
        const b = switchButton("trigger", label, app, name);
        b.addEventListener("pointerdown", () => {
          send(1);
          b.classList.remove("fired");
          void b.offsetWidth; // restart the flash
          b.classList.add("fired");
        });
        b.addEventListener("keydown", (e) => {
          if (e.key === " " || e.key === "Enter") {
            e.preventDefault();
            send(1);
          }
        });
        parts.switches.append(b);
        return { set: () => {} };
      }
    }
  }

  // A list over a continuous value: the item i is the middle of its step,
  // (i + 0.5) / n, so the Pod's knob reading lands on the same item.
  menu(list, parts, send, name) {
    const box = el("label", "menu");
    box.append(el("span", "choice-label", name));
    const select = el("select");
    list.forEach((item, i) => select.append(new Option(item, String(i))));
    select.addEventListener("change", () => send(Math.round(((Number(select.value) + 0.5) / list.length) * UNIT_MAX)));
    box.append(select);
    parts.menus.append(box);
    const n = list.length;
    return { set: (raw) => (select.value = String(Math.min(n - 1, Math.floor((raw / UNIT_MAX) * n)))) };
  }

  // A choice as a drop-down of its own options.
  choiceMenu(p, parts, send, name) {
    const box = el("label", "menu");
    box.append(el("span", "choice-label", name));
    const select = el("select");
    p.options.forEach((option, i) => select.append(new Option(option, String(i))));
    select.addEventListener("change", () => send(Number(select.value)));
    box.append(select);
    parts.menus.append(box);
    return { set: (raw) => (select.value = String(raw)), el: box };
  }

  state(values) {
    this.values = values;
    values.forEach((raw, i) => this.widgets[i]?.set(raw));
    for (const o of this.overrides) o.el?.classList.toggle("overridden", (values[o.by] ?? 0) > 0);
    if (this.visibility.length) {
      const value = (name) => values[this.byName.get(name)] ?? 0;
      for (const v of this.visibility) {
        const off = !this.app.groupVisible(v.name, value);
        v.el.dataset.off = String(off);
        v.el.hidden = off || (v.el.dataset.tab !== undefined && v.el.dataset.tab !== this.tabs[this.tab]);
      }
    }
  }
}

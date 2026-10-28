// The control surface, built from the parameter list the app sends: a card
// per group, holding knobs, menus, choices, toggles and triggers, laid out
// the same way for every app. A menu is a continuous parameter that picks
// from a list (the app's menu(name) gives the list), as a knob stepping
// through it does on the Pod. Changes go out through onSet(index, raw); the app's
// state messages come back in through state().
//
// Groups named "<tab>/<card>" (wavebyte's layers, "A/Filter") go in tabs,
// one tab's cards showing at a time, under a bar of the tabs. Their
// parameters may start with the tab ("B: Cutoff"), which their labels
// leave out. If the app names a choice parameter as its tabParam, the bar
// is that parameter's control: picking a tab sets it, and the app setting
// it picks the tab.
//
// An app can show a long choice as a drop-down instead (menu(name)
// returning true), and say a choice takes another's place while it isn't
// 0 (overrides(name), giving the other's name): the other then shows as
// not in use.

import { createKnob } from "./knob.js";
import { Kind, UNIT_MAX } from "./protocol.js";

const SEND_MS = 25; // at most this often per knob while it's turning

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

const TABBED = /^([^/]+)\/(.+)$/;

const el = (tag, className) => {
  const e = document.createElement(tag);
  if (className) e.className = className;
  return e;
};

function optionButton(param, app, index, label, onPick) {
  const b = el("button", "option");
  b.type = "button";
  b.textContent = label;
  b.setAttribute("aria-pressed", "false");
  const colour = app.optionColour?.(param.name, index, param.options[index]);
  if (colour) b.style.setProperty("--option", colour);
  b.addEventListener("click", () => onPick(index));
  return b;
}

// A choice as a segmented control, or, for options like "Mosaic A" ...
// "Warp D", a grid with the names across and the variations down.
function optionButtons(param, app, onPick, wrap) {
  const buttons = [];
  const rows = matrixRows(param.options, param.columns);
  let root;
  if (rows) {
    root = el("div", "matrix-scroll");
    const grid = el("div", "matrix");
    grid.style.setProperty("--names", String(rows.length));
    grid.append(el("span"));
    for (const row of rows) {
      const name = el("span", "matrix-name");
      name.textContent = row.name;
      grid.append(name);
    }
    for (let c = 0; c < param.columns; c++) {
      const letter = el("span", "matrix-variation");
      letter.textContent = rows[0].cells[c].label;
      const colour = app.optionColour?.(param.name, rows[0].cells[c].index);
      if (colour) letter.style.setProperty("--option", colour);
      grid.append(letter);
      for (const row of rows) {
        const cell = row.cells[c];
        const b = optionButton(param, app, cell.index, cell.label, onPick);
        b.setAttribute("aria-label", param.options[cell.index]);
        buttons[cell.index] = b;
        grid.append(b);
      }
    }
    root.append(grid);
  } else {
    root = el("div", wrap ? "segmented options options-wrap" : "segmented options");
    root.style.setProperty("--columns", String(param.columns || param.options.length));
    param.options.forEach((o, i) => root.append((buttons[i] = optionButton(param, app, i, o, onPick))));
  }
  root.setAttribute("role", "group");
  root.setAttribute("aria-label", param.name);

  const select = (index) => buttons.forEach((b, i) => b.setAttribute("aria-pressed", String(i === index)));
  return { el: root, select };
}

// A switch: a toggle with a light showing its state, or a trigger with a ring.
function switchButton(kind, label) {
  const b = el("button", kind);
  b.type = "button";
  const text = el("span");
  text.textContent = label;
  b.append(el("span", "led"), text);
  return b;
}

export class Controls {
  constructor(root, onSet) {
    this.root = root;
    this.onSet = onSet;
    this.widgets = [];
    this.tabs = [];
    this.tab = 0;
    this.tabBar = null;
    this.overrides = []; // { el: the choice taken over, by: the index of the one taking it }
  }

  // Three zones, the same for every app: a group that is one big choice
  // (grainfall's effects, tapeworn's tape models) across the top, the knob
  // pages in a row, and everything else in columns. Inside a card, always
  // knobs, then menus, then choices, then switches.
  build(hello, params, app) {
    this.root.replaceChildren();
    this.widgets = [];
    this.overrides = [];
    this.tabs = [];
    for (const name of hello.groups) {
      const m = TABBED.exec(name);
      if (m && !this.tabs.includes(m[1])) this.tabs.push(m[1]);
    }
    const tabParam = this.tabs.length > 0 && app.tabParam ? params.find((p) => p && p.name === app.tabParam) : undefined;
    const tabCards = el("div", "controls-more");
    const zones = {
      main: el("div", "controls-main"),
      tabbed: el("div", "controls-tabbed"),
      pages: el("div", "controls-pages"),
      more: el("div", "controls-more"),
    };
    if (this.tabs.length) zones.tabbed.append(this.buildTabBar(tabParam, app), tabCards);
    hello.groups.forEach((fullName, g) => {
      const members = params.filter((p) => p && p.group === g && p !== tabParam);
      if (members.length === 0) return;
      const tabbed = TABBED.exec(fullName);
      const tab = tabbed?.[1];
      const groupName = tabbed ? tabbed[2] : fullName;
      // A tab's parameters, without the tab in front.
      const nameOf = (p) => (tab && p.name.startsWith(`${tab}: `) ? p.name.slice(tab.length + 2) : p.name);

      const page = /^Page (\d+)$/.exec(groupName);
      const main = members.length === 1 && members[0].kind === Kind.CHOICE && members[0].options.length > 8;
      // A page is titled by its knobs (the chip has its number), or by what's
      // on it if it has none.
      const knobNames = members.filter((m) => m.kind === Kind.CONTINUOUS).map(nameOf);
      const pageNames = knobNames.length ? knobNames : members.map(nameOf);
      const title = app.groupTitle?.(groupName, members.map(nameOf)) || (page ? pageNames.join(" · ") : groupName);

      const card = el("section", "group");
      const colour = app.groupColour?.(groupName);
      if (colour) card.style.setProperty("--accent", colour);
      const head = el("div", "group-head");
      if (page) {
        const chip = el("span", "chip");
        chip.textContent = page[1];
        chip.setAttribute("aria-label", groupName);
        head.append(chip);
      }
      const h2 = el("h2");
      h2.textContent = title;
      head.append(h2);
      const meta = main ? el("span", "panel-meta group-value") : null;
      if (meta) head.append(meta);
      card.setAttribute("aria-label", page ? `${groupName}: ${title}` : title);

      const parts = {
        knobs: el("div", "group-knobs"),
        menus: el("div", "group-menus"),
        choices: el("div", "group-choices"),
        switches: el("div", "group-switches"),
      };
      for (const p of members) {
        const name = nameOf(p);
        const showLabel = !main && name !== groupName && name !== title;
        this.widgets[p.index] = this.widget(p, app, parts, { showLabel, wrap: main, meta, name });
      }
      for (const p of members) {
        const over = app.overrides?.(nameOf(p));
        const target = over && members.find((m) => nameOf(m) === over);
        if (target) this.overrides.push({ el: this.widgets[target.index]?.el, by: p.index });
      }
      card.append(head, ...Object.values(parts).filter((part) => part.childElementCount));
      if (tab) {
        card.dataset.tab = tab;
        card.hidden = tab !== this.tabs[this.tab];
        tabCards.append(card);
      } else (main ? zones.main : page ? zones.pages : zones.more).append(card);
    });
    this.root.append(...Object.values(zones).filter((zone) => zone.childElementCount));
  }

  // The bar of tabs, as the tab parameter's control if there is one.
  buildTabBar(tabParam, app) {
    const bar = el("div", "controls-tabs");
    const label = el("span", "choice-label");
    label.textContent = tabParam?.name ?? "Tab";
    const param = { name: label.textContent, options: this.tabs, columns: this.tabs.length };
    const opts = optionButtons(param, app, (i) => {
      this.showTab(i);
      if (tabParam) this.onSet(tabParam.index, i);
    });
    this.tabBar = opts;
    bar.append(label, opts.el);
    if (tabParam) this.widgets[tabParam.index] = { set: (raw) => this.showTab(raw) };
    this.tab = Math.min(this.tab, this.tabs.length - 1);
    opts.select(this.tab);
    return bar;
  }

  showTab(i) {
    if (i < 0 || i >= this.tabs.length) return;
    this.tab = i;
    this.tabBar?.select(i);
    for (const card of this.root.querySelectorAll(".group[data-tab]")) card.hidden = card.dataset.tab !== this.tabs[i];
  }

  widget(p, app, parts, { showLabel, wrap, meta, name = p.name }) {
    const send = (raw) => this.onSet(p.index, raw);
    switch (p.kind) {
      case Kind.CONTINUOUS: {
        const list = app.menu?.(name);
        if (list) return this.menu(p, list, parts, send, name);
        const out = throttled(send);
        const knob = createKnob({
          label: name,
          format: app.format?.(name),
          onInput: (v) => out(v * UNIT_MAX),
        });
        parts.knobs.append(knob.el);
        return { set: (raw) => knob.set(raw / UNIT_MAX) };
      }
      case Kind.CHOICE: {
        if (app.menu?.(name) === true) return this.choiceMenu(p, parts, send, name);
        const box = el("div", "choice");
        if (showLabel) {
          const label = el("div", "choice-label");
          label.textContent = name;
          box.append(label);
        }
        const pick = (i) => {
          opts.select(i);
          if (meta) meta.textContent = p.options[i] ?? "";
        };
        const opts = optionButtons(p, app, (i) => {
          pick(i);
          send(i);
        }, wrap);
        box.append(opts.el);
        parts.choices.append(box);
        return { set: (raw) => pick(raw), el: box };
      }
      case Kind.TOGGLE: {
        const b = switchButton("toggle", name);
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
        const b = switchButton("trigger", name);
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
  menu(p, list, parts, send, name = p.name) {
    const box = el("label", "menu");
    const label = el("span", "choice-label");
    label.textContent = name;
    const select = el("select");
    list.forEach((name, i) => {
      const o = el("option");
      o.value = String(i);
      o.textContent = name;
      select.append(o);
    });
    select.addEventListener("change", () => send(Math.round(((Number(select.value) + 0.5) / list.length) * UNIT_MAX)));
    box.append(label, select);
    parts.menus.append(box);
    const n = list.length;
    return { set: (raw) => (select.value = String(Math.min(n - 1, Math.floor((raw / UNIT_MAX) * n)))) };
  }

  // A choice as a drop-down of its own options.
  choiceMenu(p, parts, send, name) {
    const box = el("label", "menu");
    const label = el("span", "choice-label");
    label.textContent = name;
    const select = el("select");
    p.options.forEach((option, i) => {
      const o = el("option");
      o.value = String(i);
      o.textContent = option;
      select.append(o);
    });
    select.addEventListener("change", () => send(Number(select.value)));
    box.append(label, select);
    parts.menus.append(box);
    return { set: (raw) => (select.value = String(raw)), el: box };
  }

  state(values) {
    values.forEach((raw, i) => this.widgets[i]?.set(raw));
    for (const o of this.overrides) o.el?.classList.toggle("overridden", (values[o.by] ?? 0) > 0);
  }
}

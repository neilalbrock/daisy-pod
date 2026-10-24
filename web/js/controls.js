// The control surface, built from the parameter list the app sends: a card
// per group, holding knobs, option buttons, toggles and triggers. Changes go
// out through onSet(index, raw); the app's state messages come back in
// through state().

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

function optionButtons(param, app, onPick) {
  const buttons = [];
  const button = (label, index) => {
    const b = document.createElement("button");
    b.type = "button";
    b.className = "option";
    b.textContent = label;
    b.setAttribute("aria-pressed", "false");
    const colour = app.optionColour?.(param.name, index, param.options[index]);
    if (colour) b.style.setProperty("--option", colour);
    b.addEventListener("click", () => onPick(index));
    buttons[index] = b;
    return b;
  };

  const el = document.createElement("div");
  const rows = matrixRows(param.options, param.columns);
  if (rows) {
    el.className = "matrix";
    el.style.setProperty("--columns", String(param.columns));
    for (const row of rows) {
      const name = document.createElement("div");
      name.className = "matrix-name";
      name.textContent = row.name;
      el.append(name, ...row.cells.map((c) => button(c.label, c.index)));
    }
  } else {
    el.className = "options";
    el.style.setProperty("--columns", String(param.columns || param.options.length));
    param.options.forEach((o, i) => el.append(button(o, i)));
  }
  el.setAttribute("role", "group");
  el.setAttribute("aria-label", param.name);

  const select = (index) =>
    buttons.forEach((b, i) => b.setAttribute("aria-pressed", String(i === index)));
  return { el, select };
}

export class Controls {
  constructor(root, onSet) {
    this.root = root;
    this.onSet = onSet;
    this.widgets = [];
  }

  build(hello, params, app) {
    this.root.replaceChildren();
    this.widgets = [];
    hello.groups.forEach((groupName, g) => {
      const members = params.filter((p) => p && p.group === g);
      if (members.length === 0) return;

      const card = document.createElement("section");
      // A big grid of options (grainfall's effects) gets room of its own.
      const big = members.some((m) => m.kind === Kind.CHOICE && matrixRows(m.options, m.columns));
      // A long list (tapeworn's tape models) runs down beside the others.
      const tall = !big && members.some((m) => m.kind === Kind.CHOICE && m.options.length > 8);
      card.className = big ? "group group-wide" : tall ? "group group-tall" : "group";
      const colour = app.groupColour?.(groupName);
      if (colour) card.style.setProperty("--accent", colour);
      const title = document.createElement("h2");
      title.textContent = app.groupTitle?.(groupName, members.map((m) => m.name)) || groupName;
      const body = document.createElement("div");
      body.className = "group-body";
      card.append(title, body);
      this.root.append(card);

      for (const p of members) this.widgets[p.index] = this.widget(p, app, body);
    });
  }

  widget(p, app, body) {
    const send = (raw) => this.onSet(p.index, raw);
    switch (p.kind) {
      case Kind.CONTINUOUS: {
        const out = throttled(send);
        const knob = createKnob({
          label: p.name,
          format: app.format?.(p.name),
          onInput: (v) => out(v * UNIT_MAX),
        });
        body.append(knob.el);
        return { set: (raw) => knob.set(raw / UNIT_MAX) };
      }
      case Kind.CHOICE: {
        const wrap = document.createElement("div");
        wrap.className = "choice";
        const label = document.createElement("div");
        label.className = "choice-label";
        label.textContent = p.name;
        label.hidden = p.name === body.parentElement.querySelector("h2").textContent;
        const opts = optionButtons(p, app, (i) => {
          opts.select(i);
          send(i);
        });
        wrap.append(label, opts.el);
        body.append(wrap);
        return { set: (raw) => opts.select(raw) };
      }
      case Kind.TOGGLE: {
        const b = document.createElement("button");
        b.type = "button";
        b.className = "toggle";
        b.textContent = p.name;
        b.setAttribute("aria-pressed", "false");
        b.addEventListener("click", () => {
          const on = b.getAttribute("aria-pressed") !== "true";
          b.setAttribute("aria-pressed", String(on));
          send(on ? 1 : 0);
        });
        body.append(b);
        return { set: (raw) => b.setAttribute("aria-pressed", String(raw !== 0)) };
      }
      default: {
        const b = document.createElement("button");
        b.type = "button";
        b.className = "trigger";
        b.textContent = p.name;
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
        body.append(b);
        return { set: () => {} };
      }
    }
  }

  state(values) {
    values.forEach((raw, i) => this.widgets[i]?.set(raw));
  }
}

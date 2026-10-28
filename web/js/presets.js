// The presets, in the header, for apps that have them (common/presets.h):
// a button per slot with a stripe in its LED colour, dimmed while empty,
// and Save. Clicking a slot selects it, loading its preset (an empty one
// keeps the sound); clicking the selected slot again reloads it, dropping
// changes. Save writes the current settings to the selected slot. Factory
// reset (in Setup), once confirmed, empties every slot and puts the saved
// settings back to their defaults.

// Hue(slot / count) as LED 2 shows it on the Pod (common/colour.h).
function hue(h) {
  const x = h * 6;
  const c = (v) => Math.round(255 * Math.min(Math.max(v, 0), 1));
  return `rgb(${c(Math.abs(x - 3) - 1)} ${c(2 - Math.abs(x - 2))} ${c(2 - Math.abs(x - 4))})`;
}

export class PresetsPanel {
  constructor(root, reset, { onSelect, onSave, onReset }) {
    this.root = root;
    this.reset = reset;
    this.slots = root.querySelector(".preset-slots");
    this.meta = root.querySelector(".preset-meta");
    this.save = root.querySelector(".preset-save");
    this.onSelect = onSelect;
    this.buttons = [];
    this.state = null;
    this.save.addEventListener("click", () => {
      if (this.state) onSave(Math.max(this.state.slot, 0));
    });
    reset.addEventListener("click", () => {
      if (window.confirm("Erase every preset and saved setting for this app, and go back to its defaults?")) onReset();
    });
  }

  // From the hello: how many slots the app has (0 hides them).
  setup(count) {
    this.root.hidden = count === 0;
    this.reset.hidden = count === 0;
    this.state = null;
    this.buttons = Array.from({ length: count }, (_, i) => {
      const b = document.createElement("button");
      b.type = "button";
      b.textContent = String(i + 1);
      b.style.setProperty("--slot", hue(i / count));
      b.setAttribute("aria-pressed", "false");
      b.addEventListener("click", () => this.onSelect(i));
      return b;
    });
    this.slots.replaceChildren(...this.buttons);
    this.meta.textContent = "";
  }

  // From each frame: { slot (-1: none yet), modified, used: [bool] }.
  update(p) {
    const s = this.state;
    if (s && s.slot === p.slot && s.modified === p.modified && s.used.every((u, i) => u === p.used[i])) return;
    this.state = p;
    this.buttons.forEach((b, i) => {
      const n = i + 1;
      b.setAttribute("aria-pressed", String(i === p.slot));
      b.classList.toggle("empty", !p.used[i]);
      b.classList.toggle("modified", i === p.slot && p.modified);
      b.setAttribute("aria-label", `Preset ${n}${p.used[i] ? "" : ", empty"}${i === p.slot && p.modified ? ", modified" : ""}`);
    });
    const n = p.slot + 1;
    // The slot's dot says modified; words only for what it can't show.
    this.meta.textContent = p.slot < 0 ? "none selected" : !p.used[p.slot] ? `${n} empty` : "";
    this.meta.hidden = !this.meta.textContent;
    this.save.textContent = `Save to ${Math.max(p.slot, 0) + 1}`;
  }
}

// A rotary knob: drag up and down (Shift for fine), scroll, or use the
// arrow keys. Value 0..1. Calls onInput(value) as it moves.

const SWEEP = 270; // degrees
const DRAG_PX = 200; // pixels for the full range
const NS = "http://www.w3.org/2000/svg";

function arc(cx, cy, r, from, to) {
  const rad = (a) => ((a - 90) * Math.PI) / 180;
  const x0 = cx + r * Math.cos(rad(from));
  const y0 = cy + r * Math.sin(rad(from));
  const x1 = cx + r * Math.cos(rad(to));
  const y1 = cy + r * Math.sin(rad(to));
  const large = to - from > 180 ? 1 : 0;
  return `M ${x0} ${y0} A ${r} ${r} 0 ${large} 1 ${x1} ${y1}`;
}

export function createKnob({ label, format = (v) => `${Math.round(v * 100)}%`, onInput }) {
  const el = document.createElement("div");
  el.className = "knob";

  const svg = document.createElementNS(NS, "svg");
  svg.setAttribute("viewBox", "0 0 64 64");
  svg.setAttribute("aria-hidden", "true");
  const track = document.createElementNS(NS, "path");
  track.setAttribute("class", "knob-track");
  track.setAttribute("d", arc(32, 32, 26, -SWEEP / 2, SWEEP / 2));
  const fill = document.createElementNS(NS, "path");
  fill.setAttribute("class", "knob-fill");
  const pointer = document.createElementNS(NS, "line");
  pointer.setAttribute("class", "knob-pointer");
  pointer.setAttribute("x1", "32");
  pointer.setAttribute("y1", "32");
  pointer.setAttribute("x2", "32");
  pointer.setAttribute("y2", "12");
  const cap = document.createElementNS(NS, "circle");
  cap.setAttribute("class", "knob-cap");
  cap.setAttribute("cx", "32");
  cap.setAttribute("cy", "32");
  cap.setAttribute("r", "17");
  svg.append(track, fill, cap, pointer);

  const dial = document.createElement("div");
  dial.className = "knob-dial";
  dial.tabIndex = 0;
  dial.setAttribute("role", "slider");
  dial.setAttribute("aria-label", label);
  dial.setAttribute("aria-valuemin", "0");
  dial.setAttribute("aria-valuemax", "100");
  dial.append(svg);

  const name = document.createElement("div");
  name.className = "knob-label";
  name.textContent = label;
  const readout = document.createElement("div");
  readout.className = "knob-value";

  el.append(dial, name, readout);

  let value = 0;
  let busyUntil = 0; // ignore the Pod's echo while the user is turning it

  function render() {
    const angle = -SWEEP / 2 + value * SWEEP;
    fill.setAttribute("d", value > 0.001 ? arc(32, 32, 26, -SWEEP / 2, angle) : "");
    pointer.setAttribute("transform", `rotate(${angle} 32 32)`);
    const text = format(value);
    readout.textContent = text;
    dial.setAttribute("aria-valuenow", String(Math.round(value * 100)));
    dial.setAttribute("aria-valuetext", text);
  }

  function change(v) {
    v = Math.min(1, Math.max(0, v));
    busyUntil = performance.now() + 400;
    if (v === value) return;
    value = v;
    render();
    onInput(value);
  }

  let dragFrom = null;
  dial.addEventListener("pointerdown", (e) => {
    dial.setPointerCapture(e.pointerId);
    dial.focus();
    dragFrom = { y: e.clientY, value };
    el.classList.add("active");
  });
  dial.addEventListener("pointermove", (e) => {
    if (!dragFrom) return;
    const scale = e.shiftKey ? 0.1 : 1;
    change(dragFrom.value + ((dragFrom.y - e.clientY) / DRAG_PX) * scale);
    if (e.shiftKey) dragFrom = { y: e.clientY, value };
  });
  const end = () => {
    dragFrom = null;
    el.classList.remove("active");
  };
  dial.addEventListener("pointerup", end);
  dial.addEventListener("pointercancel", end);
  dial.addEventListener(
    "wheel",
    (e) => {
      e.preventDefault();
      change(value - Math.sign(e.deltaY) * (e.shiftKey ? 0.002 : 0.02));
    },
    { passive: false }
  );
  dial.addEventListener("keydown", (e) => {
    const step = { ArrowUp: 0.01, ArrowRight: 0.01, ArrowDown: -0.01, ArrowLeft: -0.01, PageUp: 0.1, PageDown: -0.1 }[e.key];
    if (step !== undefined) {
      e.preventDefault();
      change(value + step);
    } else if (e.key === "Home" || e.key === "End") {
      e.preventDefault();
      change(e.key === "Home" ? 0 : 1);
    }
  });

  render();
  return {
    el,
    set(v) {
      if (dragFrom || performance.now() < busyUntil) return;
      value = Math.min(1, Math.max(0, v));
      render();
    },
  };
}

// Grainfall's device: the effect and its switches, then the pages as the
// Pod groups them, with the buffer on the display.
export default {
  display: { w: 176, values: ["Effect", "Time", "Reverse", "Hold"] },
  pod: ["Bypass", "Mono in"],
  sections: [
    { title: "Effect", group: "Effect", cols: [[{ name: "Effect", w: 96, label: false }, { row: [{ name: "Reverse", w: 46 }, { name: "Hold", w: 46 }] }]] },
    { title: "Play", group: "Page 1", cols: [["Activity", "Repeats"], [{ name: "Shape", as: "tab", lines: 2, w: 112 }, { name: "Time", as: "tab", lines: 2, w: 112 }]] },
    { title: "Tone", group: "Page 3", cols: [["Filter", "Resonance"], ["Mod depth", "Mod rate"]] },
    { title: "Space", group: "Page 6", cols: [["Space", { name: "Reverb", w: 84 }]] },
    { title: "Output", group: "Page 5", cols: [["Mix", "Volume"]] },
    { title: "Tempo", group: "Tempo", cols: [["Tempo", { name: "Tap", w: 44 }]] },
  ],
};

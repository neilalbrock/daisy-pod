// Slowdrift's device: the reverb and its mode, the tail, and the motion,
// with the LFO and the decay drawn on the display.
export default {
  display: { w: 176, values: ["Mode", "Waveshape", "Width", "Freeze", "Sustain", "On", "Decay"] },
  pod: ["On", "Sustain", "Freeze", "Mono in"],
  sections: [
    { title: "Reverb", group: "Page 1", cols: [[{ name: "Mode", as: "tab", w: 160, label: false }, { row: ["Decay", "Mix", "X"] }]] },
    { title: "Tail", group: "Page 3", cols: [[{ name: "Width", as: "tab", w: 120, label: false }, { row: ["Stretch", "Pan"] }, { name: "Trails", w: 120 }]] },
    { title: "Motion", group: "Page 2", cols: [["Filter", "Depth"], ["Rate", { name: "Waveshape", w: 72 }]] },
  ],
};

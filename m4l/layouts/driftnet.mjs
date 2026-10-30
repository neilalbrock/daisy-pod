// Driftnet's device: MOOD's face first (three columns, each with its
// toggle, two knobs and its channel), then what MOOD hides.
const toggle = (name, text, w = 64) => ({ name, text, w });
export default {
  display: { w: 168, values: ["Wet mode", "Loop mode", "Routing", "Wet on", "Looper", "Freeze", "Overdub"] },
  pod: ["True bypass"],
  banks: [
    {
      name: "MOOD",
      sections: [
        { title: "Wet", group: "Page 1", cols: [[{ name: "Wet mode", as: "tab", w: 132, label: false }, { row: ["Time", "Wet modify"] }, { row: [toggle("Wet on", "On"), toggle("Freeze", "Freeze")] }]] },
        { title: "Clock", group: "Page 3", cols: [[{ name: "Routing", as: "tab", w: 132, label: false }, { row: ["Clock", "Mix"] }]] },
        { title: "Loop", group: "Page 2", cols: [[{ name: "Loop mode", as: "tab", w: 132, label: false }, { row: ["Length", "Loop modify"] }, { row: [toggle("Looper", "Play"), toggle("Overdub", "Overdub")] }]] },
      ],
    },
    {
      name: "Options",
      sections: [
        { title: "Hidden options", group: "Hidden options", cols: [["Tone", "Stereo width"], ["Direct loop", "Fade"], ["Level balance"], [{ name: "Sync", w: 92 }, { name: "Spread solo", as: "tab", w: 92 }, { name: "Loop length", as: "tab", w: 92 }]] },
        { title: "Tempo", group: "Tempo", cols: [["Tempo", { name: "Tap", w: 44 }], [{ name: "Time division", w: 84 }, { name: "Length division", w: 84 }, toggle("Follow MIDI clock", "MIDI clock", 84), toggle("Synced", "Synced", 84)]] },
      ],
    },
    {
      name: "DIP · Ramp",
      sections: [
        { title: "DIP switches", group: "DIP switches", cols: [["Classic", "MISO", "Spread", "Dry kill"].map((n) => toggle(n, n, 58)), ["Trails", "Latch", "No dub", "Smooth"].map((n) => toggle(n, n, 58))] },
        {
          title: "Ramping",
          group: "Ramping",
          cols: [
            ["Ramp speed", "Waveform"],
            [toggle("Ramping", "Ramping", 84), toggle("Bounce", "Bounce", 84), { name: "Sweep", as: "tab", w: 84 }, { name: "Polarity", as: "tab", w: 84 }],
            [{ label: "Moves" }, toggle("Ramp time", "Time", 70), toggle("Ramp wet modify", "Wet modify", 70), toggle("Ramp clock", "Clock", 70), toggle("Ramp loop modify", "Loop modify", 70), toggle("Ramp length", "Length", 70)],
            ["Expression", toggle("Expression on", "On", 44)],
          ],
        },
      ],
    },
    {
      name: "Synth",
      sections: [
        { title: "Synth Mode", group: "Synth Mode", cols: [[toggle("Synth", "Synth Mode", 92), { name: "Output", as: "tab", w: 92 }, { name: "Octave", w: 92 }], ["Attack", "Decay"], ["Sustain", "Release"], ["Portamento"]] },
      ],
    },
  ],
};

// Wavebyte's device: one page per part of the voice, as the Pod's pages
// are, the mod matrix as a table, and the effects one at a time (the one
// a slot picks comes to the front). The layer picker stays on the left.
const osc = (n) => ({
  title: `Osc ${n}`,
  group: `A/Osc ${n}`,
  cols: [
    [{ name: `Table ${n}`, w: 96 }, { name: `AKWF ${n}`, w: 96 }, { row: [{ name: `Limit Waves ${n}`, text: "Limit", w: 46 }, { name: `Fixed ${n}`, text: "Fixed", w: 46 }] }],
    [{ name: `Wave ${n}`, short: "Wave" }, { name: `Env Amnt ${n}`, short: "Env amt" }],
    [{ name: `Level ${n}`, short: "Level" }, { name: `Vel ${n}`, short: "Vel" }],
    [{ name: `Semitone ${n}`, short: "Semi" }, { name: `Detune ${n}`, short: "Detune" }],
  ],
});
const adsr = (part, group) => ({
  title: `${part} env`,
  group,
  cols: [
    [{ name: `${part} Attack`, short: "Attack" }, { name: `${part} Sustain`, short: "Sustain" }],
    [{ name: `${part} Decay`, short: "Decay" }, { name: `${part} Release`, short: "Release" }],
  ],
});
const lfo = (n) => ({
  title: `LFO ${n}`,
  group: "A/LFOs",
  cols: [[{ name: `LFO ${n} Speed`, short: "Speed" }], [`Shape`, `Ratio`, `Mode`].map((p) => ({ name: `LFO ${n} ${p}`, w: 72, short: p }))],
});
const slot = (n) => ({
  row: [
    { label: `${n}`, w: 9 },
    { name: `Mod ${n} Source`, label: false, w: 74 },
    { name: `Mod ${n} Amount`, as: "num", w: 38 },
    { name: `Mod ${n} Target`, label: false, w: 74 },
  ],
  justify: "start",
});
const slots = (from) => [
  { row: [{ label: "", w: 9 }, { label: "Source", w: 74 }, { label: "Amount", w: 38 }, { label: "Target", w: 74 }], justify: "start" },
  ...[0, 1, 2, 3].map((i) => slot(from + i)),
];
const fx = (title, group, cols) => ({ name: title, group, cols });
const dials = (prefix, names) => names.map((n) => ({ name: `${prefix} ${n}`, short: n }));
const pairs = (list) => list.reduce((cols, d, i) => (i % 2 ? cols[cols.length - 1].push(d) : cols.push([d]), cols), []);

export default {
  display: {
    w: 168,
    values: [
      "Layer", "Layer Mode", "Table 1", "Table 2", "Filter Type", "FX 1 Type", "FX 2 Type",
      "Wave Attack", "Wave Decay", "Wave Sustain", "Wave Release",
      "Filter Attack", "Filter Decay", "Filter Sustain", "Filter Release",
      "Amp Attack", "Amp Decay", "Amp Sustain", "Amp Release",
    ],
  },
  pod: [{ name: "Layer", as: "tab", w: 60, label: false }, { name: "Active", w: 60 }, { name: "Play", w: 60 }],
  banks: [
    { name: "Oscillators", sections: [osc(1), osc(2), { title: "Voice", group: "A/Pitch", cols: [[{ name: "Voice", as: "tab", lines: 3, w: 56, label: false }, { name: "Osc Mode", as: "tab", lines: 2, w: 56 }]] }] },
    {
      name: "Filter · Amp",
      sections: [
        { title: "Filter", group: "A/Filter", cols: [[{ name: "Filter Type", as: "tab", w: 140, label: false }, { row: ["Cutoff", "Resonance", "Keytrack"] }], ["Dirt", { name: "Dirt Type", w: 64 }]] },
        { title: "Env & glide", group: "A/Env & Glide", cols: [[{ name: "Filter Env", short: "Filt env" }, { name: "Filter Vel", short: "Filt vel" }], ["Glide", { name: "Bend Range", short: "Bend" }], [{ name: "Glide Mode", as: "tab", lines: 2, w: 52 }]] },
        { title: "Amp", group: "A/Amp", cols: [["Volume", "Pan"], [{ name: "Amp Vel", short: "Vel" }, { name: "Pan Mode", w: 52 }]] },
        { title: "Character", group: "Global", cols: [["Flavour", "DAC"], ["Master"]] },
      ],
    },
    { name: "Env · LFO", sections: [adsr("Wave", "A/Envelopes"), adsr("Filter", "A/Envelopes"), adsr("Amp", "A/Envelopes"), lfo(1), lfo(2)] },
    { name: "Matrix", sections: [{ title: "Mod matrix", group: "A/Mod Matrix", cols: [slots(1), slots(5)] }] },
    {
      name: "FX",
      sections: [
        { title: "Slots", group: "FX", cols: [[{ name: "FX 1 Type", w: 84 }, { name: "FX 2 Type", w: 84 }, { name: "FX Routing", w: 84 }]] },
        {
          title: "Effect",
          group: "FX",
          pages: {
            key: "fx",
            follow: ["FX 1 Type", "FX 2 Type"],
            list: [
              fx("Chorus", "Chorus", [...pairs(dials("Chorus", ["Dry/Wet", "Depth", "Speed", "Feedback", "Center", "Spread", "High-Cut", "Low-Cut"])), [{ name: "Chorus Shape", w: 64, short: "Shape" }, { name: "Chorus Stages", w: 64, short: "Stages" }]]),
              fx("Flanger", "Flanger", [...pairs(dials("Flanger", ["Dry/Wet", "Depth", "Speed", "Feedback", "Center", "Phase Diff"])), [{ name: "Flanger Shape", w: 64, short: "Shape" }]]),
              fx("Phaser", "Phaser", [...pairs(dials("Phaser", ["Dry/Wet", "Depth", "Speed", "Feedback", "Center", "Spread", "Phase Diff"])), [{ name: "Phaser Shape", w: 64, short: "Shape" }, { name: "Phaser Stages", w: 64, short: "Stages" }, { name: "Phaser Model", w: 64, short: "Model" }]]),
              fx("Tremolo", "Tremolo", [...pairs(dials("Tremolo", ["Depth", "Speed", "Phase Diff", "Smooth"])), [{ name: "Tremolo Shape", w: 64, short: "Shape" }]]),
              fx("Drive", "Drive", [...pairs(dials("Drive", ["Dry/Wet", "Amount", "Gain"])), [{ name: "Drive Type", w: 64, short: "Type" }]]),
              fx("Compr", "Compressor", [...pairs(dials("Compr", ["Ratio", "Threshold", "InGain", "OutGain", "Attack", "Release", "Look Ahead"])), [{ name: "Compr Auto Gain", w: 64, short: "Auto gain" }]]),
              fx("EQ", "EQ", [["Dry/Wet", "Gain"], ["Low Freq", "Low Gain"], ["Mid Freq", "Mid Gain"], ["Mid Q"], ["Mid 2 Freq", "Mid 2 Gain"], ["Mid 2 Q"], ["High Freq", "High Gain"]].map((col) => dials("EQ", col))),
              fx("Delay", "Delay", [...pairs(dials("Delay", ["Dry/Wet", "Left", "Right", "Feedback", "HighCut", "LowCut"])), [{ name: "Delay Ratio Left", w: 64, short: "Ratio L" }, { name: "Delay Ratio Right", w: 64, short: "Ratio R" }], [{ name: "Delay Link", as: "tab", w: 48, short: "Link" }, { name: "Delay Sync", as: "tab", w: 48, short: "Sync" }]]),
              fx("Reverb", "Reverb", pairs(dials("Reverb", ["Dry/Wet", "Time", "Color", "Mod Depth", "Mod Rate", "Pre-Delay"]))),
            ],
          },
        },
      ],
    },
    {
      name: "Layers",
      sections: [
        { title: "Layers", group: "Global", cols: [[{ name: "Layer Mode", w: 96 }, { name: "Split Channel", w: 96 }]] },
        {
          title: "Layer actions",
          group: "*/Layer Actions",
          cols: [[{ row: [{ name: "Init Layer", text: "Init", w: 40 }, { name: "Copy Layer", text: "Copy", w: 40 }, { name: "Paste Layer", text: "Paste", w: 40 }] }, { label: "Load a layer from a preset" }, { row: [{ name: "Load Slot", w: 40, label: false }, { name: "Load From", as: "tab", w: 80, label: false }] }, { name: "Load Layer", text: "Load", w: 126 }]],
        },
        { title: "Play", group: "Play", cols: [[{ name: "Play Note", short: "Note" }, { name: "MPE", w: 44 }], ["Tempo"]] },
      ],
    },
  ],
};

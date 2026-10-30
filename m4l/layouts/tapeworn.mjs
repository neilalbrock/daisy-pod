// Tapeworn's device: the machine, then each way it wears, with tape speed
// and the saturator's loop on the display.
export default {
  display: { w: 176, values: ["Model", "AUX type", "AUX"] },
  pod: ["Bypass", "Mono in"],
  sections: [
    { title: "Machine", group: "Tape model", cols: [[{ name: "Model", w: 120 }, { name: "AUX type", as: "tab", w: 120 }, { name: "AUX", w: 120 }]] },
    { title: "Wobble", group: "Page 1", cols: [["Wow", "Flutter"]] },
    { title: "Wear", group: "Page 2", cols: [["Saturate", "Failure"]] },
    { title: "Hiss", group: "Page 4", cols: [["Noise", "Spread"]] },
    { title: "Output", group: "Page 3", cols: [["Dry", "Volume"]] },
  ],
};

// Stormsea's device: the sea, its motion, the storm and the sky, with the
// sea drawn live on the display.
export default {
  display: { w: 172, values: ["Freeze", "Calm", "Squall", "On"] },
  pod: ["On", "Trails", "Kill dry", "Mono in"],
  sections: [
    { title: "Sea", group: "Page 1", cols: [["Decay", "Size"], ["Mix", "Tone"], [{ name: "Width", as: "tab", w: 120 }, { name: "Predelay", w: 120 }, { name: "Freeze", w: 120 }]] },
    { title: "Motion", group: "Page 3", cols: [["Depth", "Tide"]] },
    { title: "Storm", group: "Page 4", cols: [["Storm", "Gusts"], ["Wind", "Rain"], [{ name: "Calm", w: 52 }, { name: "Squall", w: 52 }]] },
    { title: "Sky", group: "Page 6", cols: [[{ name: "Lightning", w: 48 }, "Distance"], [{ name: "Rumble", as: "tab", w: 126 }]] },
    { title: "Surge", group: "Page 7", cols: [["Surge", "Undertow"]] },
  ],
};

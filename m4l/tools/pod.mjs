// Pod - the device's link on the command line, to check it without Live
//
//   node m4l/tools/pod.mjs <app>                  show the Pod's values as they change
//   node m4l/tools/pod.mjs <app> <name> <value>   and set one (as Live would: 0-100 for
//                                                 a knob, an index for a choice)
//
// Takes the Pod's values as a new device does, then prints every change:
// move a knob on the Pod to see it come through. Ctrl-C to stop.

import { readFileSync } from "node:fs";

import { DeviceLink } from "../link_core.mjs";

const [app, name, value] = process.argv.slice(2);
if (!app) {
  console.error("usage: node m4l/tools/pod.mjs <app> [<name> <value>]");
  process.exit(1);
}
const spec = JSON.parse(readFileSync(new URL(`../specs/${app}.json`, import.meta.url), "utf8"));
const target = name && spec.controls.find((c) => c.name === name);
if (name && !target) {
  console.error(`${app} has no control named "${name}"`);
  process.exit(1);
}

let synced = false;
const link = new DeviceLink(spec, {
  param: (id, v) => console.log(`${spec.controls[id].name} = ${typeof v === "number" ? +v.toFixed(2) : v}`),
  status: (text) => console.log(`[${text}]`),
  presets: () => {},
  synced: () => {
    synced = true;
    if (target) {
      console.log(`setting ${target.name} to ${value}`);
      link.control(target.id, Number(value));
    }
  },
});
link.restoreDone(0); // a new device: take the Pod's values
process.on("SIGINT", () => {
  link.close();
  process.exit(synced ? 0 : 1);
});

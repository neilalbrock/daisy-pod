// Daisy link - the node.script in every device: link_core.mjs, wired to Max
//
// Its argument is the app the device is for; the device's spec is
// specs/<app>.json. Messages to and from daisy_hub.js, the device's [v8],
// which knows the controls:
//
//   in:  ctl <id> <value>       the device moved a control
//        restore <id> <value>   a value Live restored before this script ran
//        restored <synced>      that's all of them
//        pull                   take the Pod's values
//        preset <slot>, save    the Pod's preset slots
//   out: param <id> <value>     show the Pod's value
//        status <text>
//        page <key> <page>      show a section's page (an effect a slot picked)
//        display <json>         the display's state (daisy_display.js), 20 times a second
//        presets <slot> <modified> <used...>
//        synced                 the device has synced: save that with the set
//        dump                   send what Live restored

import { readFileSync } from "node:fs";

import maxApi from "max-api";

import { DeviceLink } from "./link_core.mjs";

const app = process.argv[2];
const spec = JSON.parse(readFileSync(new URL(`./specs/${app}.json`, import.meta.url), "utf8"));

let slot = -1;
let lastPresets = "";

const link = new DeviceLink(spec, {
  param: (id, value) => maxApi.outlet("param", id, value),
  status: (text) => maxApi.outlet("status", text),
  // Every frame has them: pass on only changes.
  presets: (s, modified, used) => {
    slot = s;
    const args = [s, modified ? 1 : 0, ...used.slice(0, 8).map((u) => (u ? 1 : 0))];
    if (args.join() === lastPresets) return;
    lastPresets = args.join();
    maxApi.outlet("presets", ...args);
  },
  synced: () => maxApi.outlet("synced"),
  page: (key, page) => maxApi.outlet("page", key, page),
});

const DISPLAY_MS = 50;
if (spec.display)
  setInterval(() => {
    if (!link.displayDirty) return;
    link.displayDirty = false;
    maxApi.outlet("display", JSON.stringify(link.displayState()));
  }, DISPLAY_MS);

maxApi.addHandlers({
  ctl: (id, value) => link.control(id, value),
  restore: (id, value) => link.restore(id, value),
  restored: (synced) => link.restoreDone(synced),
  pull: () => link.pull(),
  preset: (s) => link.presetSelect(s),
  save: () => {
    if (slot >= 0) link.presetSave(slot);
  },
});

process.on("exit", () => link.close());

// The hub has been holding what Live restored.
maxApi.outlet("dump");

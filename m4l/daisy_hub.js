// Daisy hub - the [v8] in every device, between its controls and daisy_link.mjs
//
// Max's own JavaScript, so it runs from the moment the device loads: it
// catches the values Live restores (the node script isn't running yet) and
// hands them over when the script asks. It finds the controls by scripting
// name, as tools/build.mjs names them: b<bank>_c<id> for a parameter's
// control, b<bank>_* for the rest of a bank's cards, and the header's by
// their own names. Controls come in as "<id> <value>", through a
// [prepend <id>].

autowatch = 0;
inlets = 1;
outlets = 1; // to the node script

var controls = {}; // id -> object
var banks = []; // bank -> its objects
var values = {}; // id -> Live value
var header = {};
var isSynced = 0; // the device has synced before
var scanned = false;

function loadbang() {
  scan();
  showBank(0);
}

function scan() {
  var o = this.patcher.firstobject;
  while (o) {
    var name = o.varname;
    var m = /^b(\d+)_(.*)$/.exec(name);
    if (m) {
      var b = parseInt(m[1], 10);
      (banks[b] = banks[b] || []).push(o);
      var c = /^c(\d+)$/.exec(m[2]);
      if (c) controls[parseInt(c[1], 10)] = o;
    } else if (name) {
      header[name] = o;
    }
    o = o.nextobject;
  }
  scanned = true;
}

function ensure() {
  if (!scanned) scan();
}

// A control moved: by hand, by automation, or by Live restoring it.
function list(id, value) {
  values[id] = value;
  outlet(0, "ctl", id, value);
}

// --- From the node script

function param(id, value) {
  ensure();
  values[id] = value;
  if (controls[id]) controls[id].message("set", value);
}

function status() {
  ensure();
  if (header.status) header.status.message("set", arrayfromargs(arguments).join(" "));
}

function presets(slot, modified) {
  ensure();
  var used = arrayfromargs(arguments).slice(2);
  for (var i = 0; i < 8; i++) {
    var button = header["preset" + i];
    if (!button) continue;
    button.message("set", i === slot ? 1 : 0);
    var level = used[i] ? 1 : 0.45;
    button.message("activetextcolor", level, level, level, 1);
    button.message("textcolor", level, level, level, 1);
  }
  if (header.presetnote) header.presetnote.message("set", slot >= 0 && modified ? "edited" : "");
}

function dump() {
  for (var id in values) outlet(0, "restore", parseInt(id, 10), values[id]);
  outlet(0, "restored", isSynced);
}

// The node script synced: keep that with the set, in a hidden toggle.
function synced() {
  ensure();
  isSynced = 1;
  if (header.synced) header.synced.message("set", 1);
}

// The hidden toggle, as Live restores it.
function stored_synced(v) {
  isSynced = v ? 1 : 0;
}

// --- The header

function presetclick(slot) {
  ensure();
  outlet(0, "preset", slot);
}

function save(v) {
  if (v) outlet(0, "save");
}

function pull(v) {
  if (v) outlet(0, "pull");
}

function bank(b) {
  ensure();
  showBank(b);
}

function showBank(shown) {
  for (var b = 0; b < banks.length; b++) {
    var objects = banks[b] || [];
    for (var i = 0; i < objects.length; i++) objects[i].hidden = b === shown ? 0 : 1;
  }
  for (var k = 0; header["bank" + k]; k++) header["bank" + k].message("set", k === shown ? 1 : 0);
}

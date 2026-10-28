#!/usr/bin/env python3
"""Wavebyte's AKWF bank: 64 tables from the AKWF single cycles.

Turns waves from AKWF-FREE (600-sample single cycles, CC0:
https://github.com/KristofferKarlAxelEkstrand/AKWF-FREE) into the recipes
for wavebyte's second bank of tables (64-127). The firmware can't carry 64
whole tables in its image (500 KB, more than the SRAM it runs from), so
this writes only what it needs to rebuild them at boot, about 110 KB:

  keys    - key waves, crossfaded across the table's 61 waves, as the
            Microwave filled its tables between key waves. From a path
            through one or more folders (every wave, ordered so each is
            the closest left to the one before, starting from the darkest,
            then a number of keys taken evenly along it, or all 61 for a
            table that steps from wave to wave), or from named waves.
  sync, fold, pwm, squeeze
          - one source wave through a family: hard sync (the cycle
            restarted 1 to 8 times a period), folding (gain into a
            triangle folder), PWM (minus a copy of itself shifted by up to
            half a cycle), or squeezing (the cycle in a narrowing window,
            formant-like). Worked out at 512 points, then band-limited to
            the 63 harmonics a 128-sample wave holds, so they come out clean.

Every wave is resampled to 128 samples by keeping harmonics 1-63 (an FFT,
so nothing aliases), and each key is rotated to line up with the one before
(the shift that correlates best), so crossfades don't cancel and 8-bit
steps are small. Waves 61-63 of each table (triangle, square, saw) are the
firmware's.

Writes apps/wavebyte/akwf_bank.{h,cpp}, which wavetables.cpp builds the
tables from (BuildKeys and BuildFamily here do what it does, so the
previews are what the Pod plays), and with --wav, a sweep through each
table played as the 8-bit oscillator plays it (nearest sample, the wave
stepped once per 16-sample block), for listening.

Needs numpy. From the repository's root, with AKWF-FREE cloned somewhere
(only its AKWF/ folder is read):

  uv run python apps/wavebyte/tools/akwf_tables.py path/to/AKWF-FREE [--wav out/]
"""
import os, sys, wave
import numpy as np

KLEN, KWAVES, H = 128, 61, 63
OVER = 512  # the families' working resolution
SR, BLOCK = 48000, 16
NAME_LEN = 10  # the Pod's own table names are no longer
NAME_BYTES = 640  # the web page gets every name in one message (720 bytes)


def load(path):
    w = wave.open(path)
    x = np.frombuffer(w.readframes(w.getnframes()), np.int16).astype(float)
    return x / 32768.0


def band_limit(x, n=KLEN):
    """Resample one cycle to n samples, keeping harmonics 1..n/2 - 1."""
    X = np.fft.rfft(x)
    Y = np.zeros(n // 2 + 1, complex)
    k = min(n // 2 - 1, len(X) - 1)
    Y[1:k + 1] = X[1:k + 1] * (n / len(x))
    return np.fft.irfft(Y, n)


def normal(y):
    y = y - y.mean()
    p = np.max(np.abs(y))
    return y / p if p > 1e-9 else y


def store(y):
    """wavetables.cpp's Store(): DC out, peak at 127, rounded."""
    y = y - y.mean()
    p = np.max(np.abs(y))
    scale = 127.0 / p if p > 1e-6 else 0.0
    return np.clip(np.round(y * scale), -127, 127).astype(np.int8)


def spectrum(y):
    m = np.abs(np.fft.rfft(y))[1:H + 1]
    return m / max(np.linalg.norm(m), 1e-12)


def centroid(y):
    m = np.abs(np.fft.rfft(y))[1:H + 1] ** 2
    return np.sum(m * np.arange(1, H + 1)) / max(np.sum(m), 1e-12)


def align(y, ref):
    """y rotated to correlate best with ref (circular)."""
    c = np.fft.irfft(np.fft.rfft(ref) * np.conj(np.fft.rfft(y)), len(y))
    return np.roll(y, int(np.argmax(c)))


def path(waves):
    """Greedy nearest-neighbour order by spectrum, from the darkest."""
    specs = [spectrum(w) for w in waves]
    left = list(range(len(waves)))
    order = [min(left, key=lambda i: centroid(waves[i]))]
    left.remove(order[0])
    while left:
        last = specs[order[-1]]
        nxt = min(left, key=lambda i: np.linalg.norm(specs[i] - last))
        order.append(nxt)
        left.remove(nxt)
    return [waves[i] for i in order]


def keys_of(waves):
    """Key waves for the firmware: aligned in turn, then stored."""
    keys = [normal(w) for w in waves]
    for i in range(1, len(keys)):
        keys[i] = align(keys[i], keys[i - 1])
    return [store(k) for k in keys]


# ---------------------------------------------------------------- What the firmware does

def build_keys(keys):
    """wavetables.cpp's BuildKeys: the keys crossfaded across the table."""
    k = [np.asarray(x, float) for x in keys]
    out = []
    for w in range(KWAVES):
        if len(k) == 1:
            out.append(store(k[0]))
            continue
        pos = w * (len(k) - 1) / (KWAVES - 1)
        a = min(int(pos), len(k) - 2)
        t = pos - a
        out.append(store((1 - t) * k[a] + t * k[a + 1]))
    return out


def source_at(src, x):
    """The source wave (128 samples, -1..1) at phases x, by linear
    interpolation, as the firmware reads it."""
    p = (x % 1.0) * KLEN
    i = np.floor(p).astype(int) % KLEN
    f = p - np.floor(p)
    return (1 - f) * src[i] + f * src[(i + 1) % KLEN]


def build_family(kind, source):
    """wavetables.cpp's BuildFamily: the source through a family, at
    OVER points, band-limited back to 128."""
    src = np.asarray(source, float) / 127.0
    x = np.arange(OVER) / OVER
    out = []
    for w in range(KWAVES):
        t = w / (KWAVES - 1)
        if kind == "sync":
            v = source_at(src, x * (1 + 7 * t))
        elif kind == "fold":
            g = (1 + 6 * t) * source_at(src, x)
            v = 1 - 4 * np.abs(((g + 1) * 0.25) % 1.0 - 0.5)
        elif kind == "pwm":
            v = source_at(src, x) - source_at(src, x + 0.48 * t)
        elif kind == "squeeze":
            width = 1 - 0.85 * t
            u = np.minimum(x / width, 1.0)
            v = np.where(x < width, source_at(src, u) * np.sqrt(np.sin(np.pi * u)), 0.0)
        else:
            raise ValueError(kind)
        out.append(store(band_limit(v)))
    return out


# ---------------------------------------------------------------- The bank

FAMILIES = ["sync", "fold", "pwm", "squeeze"]  # akwf::Kind, after kKeys

# Name (at most NAME_LEN characters), and how it's made: ("path", folders,
# keys: a number taken along the path, or "all" for 61 waves stepped
# through), ("keys", waves), or (family, source wave). Chosen by the
# numbers, not by ear: prune and reorder after listening.
BANK = [
    # Voices and instruments
    ("Voice", ("path", ["hvoice"], 12)),
    ("Voice Step", ("path", ["hvoice"], "all")),
    ("Orchestra", ("path", ["cello", "violin", "altosax", "oboe", "clarinett", "flute"], 16)),
    ("Cello", ("path", ["cello"], 12)),
    ("Violin", ("path", ["violin"], 12)),
    ("Sax", ("path", ["altosax"], 12)),
    ("Reeds", ("path", ["oboe", "clarinett"], 12)),
    ("Flute", ("path", ["flute"], 12)),
    ("Piano", ("path", ["piano"], 12)),
    ("EPiano", ("path", ["epiano"], "all")),
    ("E Organ", ("path", ["eorgan"], 16)),
    ("Clavinet", ("path", ["clavinet"], 12)),
    ("A Guitar", ("path", ["aguitar"], 12)),
    ("E Guitar", ("path", ["eguitar"], 12)),
    ("E Bass", ("path", ["ebass"], 12)),
    ("D Bass", ("path", ["dbass"], 12)),
    ("Theremin", ("path", ["theremin"], 12)),
    ("Birds", ("path", ["birds"], 12)),
    ("Stringbox", ("path", ["stringbox", "pluckalgo"], 12)),
    ("Bass Brass", ("keys", ["dbass_0010", "ebass_0030", "clavinet_0010", "altosax_0005", "eguitar_0010"])),
    # Synth shapes
    ("FM", ("path", ["fmsynth"], 16)),
    ("FM Steps", ("path", ["fmsynth"], "all")),
    ("Chip", ("path", ["vgamebasic"], "all")),
    ("Video Game", ("path", ["vgame"], 16)),
    ("Osc Chip", ("path", ["oscchip"], 16)),
    ("Bitreduce", ("path", ["bitreduced"], 16)),
    ("Distorted", ("path", ["distorted"], 12)),
    ("Granular", ("path", ["granular"], 12)),
    ("Overtone", ("path", ["overtone"], 12)),
    ("Sin Harm", ("path", ["sinharm"], 12)),
    ("Linear", ("path", ["linear"], 16)),
    ("Hand Drawn", ("path", ["hdrawn"], 16)),
    ("Symmetric", ("path", ["symetric"], 12)),
    ("Snippets", ("path", ["snippets"], 12)),
    ("Raw", ("path", ["raw"], 12)),
    ("Stereo", ("path", ["stereo"], 16)),
    ("C604", ("path", ["c604"], 12)),
    ("BW Blend", ("path", ["bw_blended"], 16)),
    ("Saw Gap", ("path", ["bw_sawgap"], 16)),
    ("Sqr Round", ("path", ["bw_squrounded"], 16)),
    ("Saw Round", ("path", ["bw_sawrounded"], 16)),
    ("BW Square", ("path", ["bw_squ"], 16)),
    ("Misc A", ("path", ["0001", "0002", "0003", "0004", "0005"], 16)),
    ("Misc B", ("path", ["0006", "0007", "0008", "0009", "0010"], 16)),
    ("Misc C", ("path", ["0011", "0012", "0013", "0014", "0015"], 16)),
    ("Misc D", ("path", ["0016", "0017", "0018", "0019", "0020"], 16)),
    # A sampled wave through a family
    ("Sync Cello", ("sync", "cello_0005")),
    ("Sync Sax", ("sync", "altosax_0010")),
    ("Sync Voice", ("sync", "hvoice_0030")),
    ("Sync Organ", ("sync", "eorgan_0040")),
    ("Sync Bass", ("sync", "dbass_0010")),
    ("Fold Voice", ("fold", "hvoice_0030")),
    ("Fold Flute", ("fold", "flute_0005")),
    ("Fold Piano", ("fold", "piano_0010")),
    ("Fold Bass", ("fold", "ebass_0030")),
    ("Fold Sax", ("fold", "altosax_0005")),
    ("PWM Organ", ("pwm", "eorgan_0040")),
    ("PWM EPiano", ("pwm", "epiano_0010")),
    ("PWM Guitar", ("pwm", "eguitar_0010")),
    ("PWM Cello", ("pwm", "cello_0005")),
    ("Sqz Sax", ("squeeze", "altosax_0010")),
    ("Sqz Violin", ("squeeze", "violin_0005")),
    ("Sqz Voice", ("squeeze", "hvoice_0030")),
    ("Sqz Clav", ("squeeze", "clavinet_0010")),
]


def folder(root, name):
    d = os.path.join(root, "AKWF", "AKWF_" + name)
    return [load(os.path.join(d, f)) for f in sorted(os.listdir(d)) if f.endswith(".wav")]


def one(root, name):
    sub = name.rsplit("_", 1)[0]
    return load(os.path.join(root, "AKWF", "AKWF_" + sub, "AKWF_" + name + ".wav"))


def recipe(root, how):
    """(kind index, the waves the firmware keeps for it)."""
    kind = how[0]
    if kind == "path":
        _, folders, count = how
        waves = path([band_limit(x) for f in folders for x in folder(root, f)])
        n = KWAVES if count == "all" else count
        if len(waves) > n:
            waves = [waves[int(round(i))] for i in np.linspace(0, len(waves) - 1, n)]
        return 0, keys_of(waves)
    if kind == "keys":
        return 0, keys_of([band_limit(one(root, w)) for w in how[1]])
    return 1 + FAMILIES.index(kind), [store(band_limit(one(root, how[1])))]


def build(kind, waves):
    return build_keys(waves) if kind == 0 else build_family(FAMILIES[kind - 1], waves[0])


def render(table, out, seconds=6.0, note=48):
    """A sweep 0 -> 60 -> 0 through the table, as the 8-bit oscillator
    plays it."""
    hz = 440 * 2 ** ((note - 69) / 12)
    n = int(seconds * SR)
    t = np.arange(n) / SR
    pos = (1 - np.abs(2 * t / seconds - 1)) * (KWAVES - 1)
    pos = np.repeat(pos[::BLOCK], BLOCK)[:n]
    i0 = ((np.arange(n) * hz / SR) % 1.0 * KLEN).astype(int) % KLEN
    w = np.minimum((pos + 0.5).astype(int), KWAVES - 1)
    y = np.array(table, float)[w, i0] / 127 * 0.5
    y *= np.minimum(1, np.minimum(t / 0.01, (seconds - t) / 0.05))
    with wave.open(out, "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(SR)
        f.writeframes((y * 32767).astype(np.int16).tobytes())


def write_bank(recipes, here):
    names = [n for n, _ in BANK]
    pool = [w for _, waves in recipes for w in waves]
    head = "// Generated by tools/akwf_tables.py from AKWF-FREE (CC0). Don't edit: rerun it.\n"
    with open(os.path.join(here, "akwf_bank.h"), "w") as f:
        f.write(head + """//
// Wavebyte's second bank of tables, from the AKWF single cycles: each a
// recipe that wavetables.cpp builds at boot, from key waves crossfaded
// across the table, or from one source wave through a family.

#pragma once

#include <cstdint>

namespace akwf
{
enum Kind : uint8_t
{
    kKeys,
    kSync,
    kFold,
    kPwm,
    kSqueeze,
};

struct Recipe
{
    Kind     kind;
    uint8_t  count; // waves in the pool: the keys, or 1 (the source)
    uint16_t first; // the first in the pool
};

constexpr int kTables = %d;
constexpr int kPoolWaves = %d;

extern const Recipe kRecipes[kTables];
extern const int8_t kPool[kPoolWaves][128];
// "Off" first, then the tables' names: the AKWF choice's options.
extern const char *const kNames[kTables + 1];
} // namespace akwf
""" % (len(BANK), len(pool)))
    with open(os.path.join(here, "akwf_bank.cpp"), "w") as f:
        f.write(head + '\n#include "akwf_bank.h"\n\nnamespace akwf\n{\n')
        f.write("const char *const kNames[kTables + 1] = {\n    \"Off\",\n")
        for n in names:
            f.write('    "%s",\n' % n)
        f.write("};\n\nconst Recipe kRecipes[kTables] = {\n")
        first = 0
        kinds = ["kKeys", "kSync", "kFold", "kPwm", "kSqueeze"]
        for (n, _), (kind, waves) in zip(BANK, recipes):
            f.write("    {%s, %d, %d}, // %s\n" % (kinds[kind], len(waves), first, n))
            first += len(waves)
        f.write("};\n\nconst int8_t kPool[kPoolWaves][128] = {\n")
        for w in pool:
            f.write("    {" + ",".join(str(int(v)) for v in w) + "},\n")
        f.write("};\n} // namespace akwf\n")
    return len(pool)


def main():
    root = sys.argv[1]
    out = sys.argv[sys.argv.index("--wav") + 1] if "--wav" in sys.argv else None
    here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    names = [n for n, _ in BANK]
    assert len(BANK) <= 127 and all(len(n) <= NAME_LEN for n in names) and len(set(names)) == len(names)
    assert sum(len(n) + 1 for n in names) + 4 <= NAME_BYTES, "the names won't fit the web page's message"

    recipes = [recipe(root, how) for _, how in BANK]
    n_pool = write_bank(recipes, here)
    print("%d tables, %d pool waves (%d KB)" % (len(BANK), n_pool, n_pool * KLEN // 1024))
    if out:
        os.makedirs(out, exist_ok=True)
    for i, ((name, _), (kind, waves)) in enumerate(zip(BANK, recipes)):
        table = build(kind, waves)
        smooth = np.mean([np.linalg.norm(spectrum(table[w + 1].astype(float)) - spectrum(table[w].astype(float)))
                          for w in range(KWAVES - 1)])
        print("%3d %-10s neighbours %.3f" % (64 + i, name, smooth))
        if out:
            render(table, os.path.join(out, "%03d_%s.wav" % (64 + i, name.lower().replace(" ", "_"))))


if __name__ == "__main__":
    main()

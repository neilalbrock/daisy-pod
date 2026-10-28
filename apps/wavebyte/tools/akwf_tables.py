#!/usr/bin/env python3
"""Wavebyte tables from AKWF single cycles: a prototype.

Turns folders of AKWF-FREE waves (600-sample single cycles, CC0:
https://github.com/KristofferKarlAxelEkstrand/AKWF-FREE) into wavebyte
tables: 61 waves of 128 8-bit samples (wavetables.h), stored as
wavetables.cpp's Store() does. Waves 61-63 (triangle, square, saw) are the
firmware's, so they aren't written here.

Ways to make a table:

  path   - every wave in the folders, ordered so each is close to the one
           before (by harmonic spectrum, starting from the darkest), so a
           sweep moves smoothly instead of jumping. Longer paths are
           sampled evenly; shorter ones are filled with crossfades between
           neighbours, as the Microwave filled its tables between key waves.
           Or a given number of key waves along the path, crossfaded.
  keys   - named waves in the given order, crossfaded in between.
  sync, fold, pwm, stretch
         - one wave run through one of wavebyte's own families (hard sync,
           wavefolding, a wave minus a shifted copy of itself, formant-style
           stretching), so the table's start is the sampled sound.
  raw    - a folder's first 61 waves in file order, unaligned, for
           comparison (the AKWF Synthesis Technology tables are made so).

Every wave is rotated to line up with the one before (the shift that
correlates best), so Clean mode's crossfades don't cancel and 8-bit steps
are small.

Writes a C++ header of int8 tables and, with --wav, a sweep through each
table played as wavebyte's 8-bit oscillator does (nearest sample, the wave
stepped once per 16-sample block) for listening.

Needs numpy. From the repository's root, with AKWF-FREE cloned somewhere
(about 2 GB; only its AKWF/ folder is read):

  python3 apps/wavebyte/tools/akwf_tables.py path/to/AKWF-FREE out/ [--wav]

Nothing in the firmware reads the header yet: see docs/akwf.md.
"""
import os, sys, wave
import numpy as np

KLEN, KWAVES, H = 128, 61, 63
SR, BLOCK = 48000, 16


def load(path):
    w = wave.open(path)
    x = np.frombuffer(w.readframes(w.getnframes()), np.int16).astype(float)
    return x / 32768.0


def band_limit(x, n=KLEN):
    """Resample one cycle to n samples, keeping harmonics 1..n/2 - 1 (the
    firmware's own waves stop there too)."""
    X = np.fft.rfft(x)
    Y = np.zeros(n // 2 + 1, complex)
    k = min(n // 2 - 1, len(X) - 1)
    Y[1:k + 1] = X[1:k + 1] * (n / len(x))
    return np.fft.irfft(Y, n)


def normal(y):
    y = y - y.mean()
    p = np.max(np.abs(y))
    return y / p if p > 1e-9 else y


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


def fill(keys, n=KWAVES):
    """n waves along a list of waves: sampled evenly if there are more,
    crossfaded between neighbours if fewer."""
    keys = [normal(k) for k in keys]
    for i in range(1, len(keys)):
        keys[i] = align(keys[i], keys[i - 1])
    if len(keys) == 1:
        return [keys[0]] * n
    out = []
    for w in range(n):
        pos = w / (n - 1) * (len(keys) - 1)
        a = min(int(pos), len(keys) - 2)
        t = pos - a
        if len(keys) >= n:  # more than we need: nearest, no blend
            out.append(keys[int(round(pos))])
        else:
            out.append(normal((1 - t) * keys[a] + t * keys[a + 1]))
    return out


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


def wave_at(x, phase):
    """A 600-sample cycle at any phase (0..1), by linear interpolation."""
    p = (phase % 1.0) * len(x)
    i = np.floor(p).astype(int)
    f = p - i
    return (1 - f) * x[i % len(x)] + f * x[(i + 1) % len(x)]


def family(x, kind, amount):
    """One wave through one of wavebyte's families, at a 0..1 amount. Works
    on the full 600-sample cycle, then band-limits, so the result is clean."""
    t = np.arange(len(x)) / len(x)
    if kind == "sync":  # the cycle restarted 1 to 8 times a period
        ratio = 1 + amount * 7
        y = wave_at(x, (t * ratio) % 1.0)
    elif kind == "fold":  # gain into a triangle folder
        g = 1 + amount * 6
        v = g * normal(x)
        y = 1 - 4 * np.abs(((v + 1) * 0.25) % 1.0 - 0.5)
    elif kind == "pwm":  # minus a copy shifted by up to half a cycle
        y = normal(x) - np.roll(normal(x), int(amount * 0.48 * len(x)))
    elif kind == "stretch":  # the cycle squeezed into a shrinking window
        w = 1 - 0.85 * amount
        y = np.where(t < w, wave_at(x, t / w), 0.0) * np.sin(np.pi * np.minimum(t / w, 1)) ** 0.5
    else:
        raise ValueError(kind)
    return band_limit(y)


def store(y):
    """wavetables.cpp's Store(): DC out, peak at 127, rounded."""
    y = normal(y)
    return np.clip(np.round(y * 127), -127, 127).astype(np.int8)


def render(table, seconds=8.0, note=48, clean=False, out=None):
    """A sweep 0 -> 60 -> 0 through the table, as wavebyte plays it."""
    hz = 440 * 2 ** ((note - 69) / 12)
    n = int(seconds * SR)
    t = np.arange(n) / SR
    tri = 1 - np.abs(2 * t / seconds - 1)  # 0 -> 1 -> 0
    pos = tri * (KWAVES - 1)
    pos = np.repeat(pos[::BLOCK], BLOCK)[:n]  # once per block
    phase = (np.arange(n) * hz / SR) % 1.0
    idx = phase * KLEN
    i0 = idx.astype(int) % KLEN
    tb = np.array(table, float)
    if clean:
        i1 = (i0 + 1) % KLEN
        f = idx - np.floor(idx)
        a = np.minimum(pos.astype(int), KWAVES - 1)
        b = np.minimum(a + 1, KWAVES - 1)
        m = pos - a
        x0 = (1 - f) * tb[a, i0] + f * tb[a, i1]
        x1 = (1 - f) * tb[b, i0] + f * tb[b, i1]
        y = (1 - m) * x0 + m * x1
    else:
        w = np.minimum((pos + 0.5).astype(int), KWAVES - 1)
        y = tb[w, i0]
    y = y / 127 * 0.5
    fade = np.minimum(1, np.minimum(t / 0.01, (seconds - t) / 0.05))
    y *= fade
    with wave.open(out, "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(SR)
        f.writeframes((y * 32767).astype(np.int16).tobytes())


def folder(root, name):
    d = os.path.join(root, "AKWF", name)
    return [load(os.path.join(d, f)) for f in sorted(os.listdir(d)) if f.endswith(".wav")]


def one(root, name):
    sub = name.rsplit("_", 1)[0]
    return load(os.path.join(root, "AKWF", sub, name + ".wav"))


# Name, and how it's made. A dozen to show the ideas; a real set would be
# curated by ear.
TABLES = [
    # (folders, key waves): None keeps every wave along the path (it steps
    # from one to the next); a number takes that many along it, crossfaded.
    ("AKWF Voice", ("path", (["AKWF_hvoice"], 12))),
    ("AKWF Voice Steps", ("path", (["AKWF_hvoice"], None))),
    ("AKWF Orchestra", ("path", (["AKWF_cello", "AKWF_violin", "AKWF_altosax",
                                  "AKWF_oboe", "AKWF_clarinett", "AKWF_flute"], 16))),
    ("AKWF EPiano", ("path", (["AKWF_epiano"], None))),
    ("AKWF FM", ("path", (["AKWF_fmsynth"], 16))),
    ("AKWF Chip", ("path", (["AKWF_vgamebasic"], None))),
    ("Sync Cello", ("sync", "AKWF_cello_0005")),
    ("Fold Voice", ("fold", "AKWF_hvoice_0030")),
    ("Self PWM Organ", ("pwm", "AKWF_eorgan_0040")),
    ("Squeeze Sax", ("stretch", "AKWF_altosax_0010")),
    ("Bass to Brass", ("keys", ["AKWF_dbass_0010", "AKWF_ebass_0030", "AKWF_clavinet_0010",
                                "AKWF_altosax_0005", "AKWF_eguitar_0010"])),
    # For comparison: a folder in file order, as the AKWF Synthesis
    # Technology tables are, with no ordering.
    ("Unordered 0001", ("raw", ["AKWF_0001"])),
]


def make(root, how):
    kind, arg = how
    if kind == "path":
        folders, n_keys = arg
        waves = path([band_limit(x) for f in folders for x in folder(root, f)])
        if n_keys:  # key waves along the path, crossfaded
            waves = [waves[int(round(i))] for i in np.linspace(0, len(waves) - 1, n_keys)]
        return fill(waves)
    if kind == "raw":
        waves = [band_limit(x) for f in arg for x in folder(root, f)][:KWAVES]
        return [normal(w) for w in waves]
    if kind == "keys":
        return fill([band_limit(one(root, k)) for k in arg])
    x = one(root, arg)
    return fill([family(x, kind, a) for a in np.linspace(0, 1, KWAVES)])


def main():
    root, out = sys.argv[1], sys.argv[2]
    wav = "--wav" in sys.argv
    os.makedirs(out, exist_ok=True)
    lines = ["// Generated by akwf_tables.py from AKWF-FREE (CC0). %d tables x %d waves x %d."
             % (len(TABLES), KWAVES, KLEN), "#pragma once", "#include <cstdint>", ""]
    lines.append("const char *const kAkwfNames[%d] = {%s};" %
                 (len(TABLES), ", ".join('"%s"' % n for n, _ in TABLES)))
    lines.append("const int8_t kAkwfWaves[%d][%d][%d] = {" % (len(TABLES), KWAVES, KLEN))
    for name, how in TABLES:
        table = [store(w) for w in make(root, how)]
        smooth = np.mean([np.linalg.norm(spectrum(table[i + 1].astype(float)) - spectrum(table[i].astype(float)))
                          for i in range(KWAVES - 1)])
        print("%-16s neighbours %.3f" % (name, smooth))
        lines.append("  { // %s" % name)
        for w in table:
            lines.append("    {" + ",".join(str(int(v)) for v in w) + "},")
        lines.append("  },")
        if wav:
            slug = name.lower().replace(" ", "_")
            render(table, out=os.path.join(out, slug + ".wav"))
            if name in ("AKWF Voice", "Sync Cello"):
                render(table, clean=True, out=os.path.join(out, slug + "_clean.wav"))
    lines.append("};")
    with open(os.path.join(out, "akwf_tables.h"), "w") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()

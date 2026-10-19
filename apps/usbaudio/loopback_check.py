#!/usr/bin/env python3
"""Loopback test for UsbAudio (see README.md).

    loopback_check.py make-signal noise.wav [--minutes 30]
    loopback_check.py compare noise.wav recording.wav

make-signal writes stereo 24-bit, 48 kHz white noise. Play it to the Pod in
loopback mode and record the Pod at the same time, then run compare. It finds
the round-trip delay and checks every sample. The Pod passes samples through
untouched, so the recording should match the source exactly. Each mismatch
is reported with its time, and so is any slip (samples dropped or repeated,
which shifts everything after it).

Needs numpy. The recording can be 24-bit or 32-bit integer, or 32-bit float.
"""

import argparse
import struct
import sys

import numpy as np

RATE = 48000
BLOCK = 4800  # frames compared at a time (100 ms)
MAX_SLIP = 200  # frames searched either side after a mismatch


class Wav:
    """A WAV file's samples, read lazily as 24-bit integers."""

    def __init__(self, path):
        with open(path, "rb") as f:
            riff, _, wave = struct.unpack("<4sI4s", f.read(12))
            if riff != b"RIFF" or wave != b"WAVE":
                sys.exit(f"{path}: not a WAV file")
            fmt = None
            while True:
                header = f.read(8)
                if len(header) < 8:
                    sys.exit(f"{path}: no data chunk")
                cid, size = struct.unpack("<4sI", header)
                if cid == b"fmt ":
                    fmt = f.read(size)
                elif cid == b"data":
                    offset = f.tell()
                    break
                else:
                    f.seek(size, 1)
                if size % 2:
                    f.seek(1, 1)
        if fmt is None:
            sys.exit(f"{path}: no fmt chunk")

        tag, self.channels, self.rate = struct.unpack("<HHI", fmt[:8])
        bits = struct.unpack("<H", fmt[14:16])[0]
        if tag == 0xFFFE:  # WAVE_FORMAT_EXTENSIBLE: real tag starts the GUID
            tag = struct.unpack("<H", fmt[24:26])[0]
        if (tag, bits) not in [(1, 24), (1, 32), (3, 32)]:
            sys.exit(f"{path}: {bits}-bit format {tag} isn't supported "
                     "(record 24-bit, 32-bit or 32-bit float)")
        if self.rate != RATE:
            sys.exit(f"{path}: {self.rate} Hz, expected {RATE}")

        self.kind = {(1, 24): "int24", (1, 32): "int32", (3, 32): "float"}[
            (tag, bits)]
        self.width = bits // 8
        n = size // (self.width * self.channels)
        self.frames = n
        self.raw = np.memmap(path, np.uint8, "r", offset,
                             n * self.width * self.channels)

    def read(self, start, stop):
        """Frames [start, stop) as an int32 array of shape (n, channels)."""
        start, stop = max(start, 0), min(stop, self.frames)
        if stop <= start:
            return np.zeros((0, self.channels), np.int32)
        step = self.width * self.channels
        b = np.asarray(self.raw[start * step:stop * step])
        if self.kind == "int24":
            b = b.reshape(-1, 3).astype(np.int32)
            s = (b[:, 0] << 8 | b[:, 1] << 16 | b[:, 2] << 24) >> 8
        elif self.kind == "int32":
            s = b.view("<i4") >> 8
        else:
            s = np.rint(b.view("<f4").astype(np.float64) * 8388608.0)
            s = np.clip(s, -8388608, 8388607).astype(np.int32)
        return s.reshape(-1, self.channels)


def make_signal(path, minutes):
    frames = int(minutes * 60 * RATE)
    rng = np.random.default_rng(1)
    with open(path, "wb") as f:
        size = frames * 6
        f.write(struct.pack("<4sI4s", b"RIFF", 36 + size, b"WAVE"))
        f.write(struct.pack("<4sIHHIIHH", b"fmt ", 16, 1, 2, RATE,
                            RATE * 6, 6, 24))
        f.write(struct.pack("<4sI", b"data", size))
        for start in range(0, frames, RATE * 10):
            n = min(RATE * 10, frames - start)
            # About -6 dBFS peak, so nothing clips on the way
            s = rng.integers(-(1 << 22), 1 << 22, (n, 2), dtype=np.int32)
            b = s.astype("<i4").view(np.uint8).reshape(n, 2, 4)[:, :, :3]
            f.write(b.tobytes())
    print(f"Wrote {minutes} minutes of noise to {path}")


def find_delay(src, rec):
    """Frames by which the recording lags the source, by cross-correlation."""
    a = src.read(RATE, 3 * RATE)[:, 0].astype(np.float64)
    b = rec.read(0, 5 * RATE)[:, 0].astype(np.float64)
    n = 1 << int(np.ceil(np.log2(len(a) + len(b))))
    corr = np.fft.irfft(np.fft.rfft(b, n) * np.conj(np.fft.rfft(a, n)), n)
    lag = int(np.argmax(corr))
    if lag > n // 2:
        lag -= n
    return lag - RATE  # the source excerpt started 1 s in


def compare(src_path, rec_path):
    src, rec = Wav(src_path), Wav(rec_path)
    if src.channels != 2 or rec.channels != 2:
        sys.exit("Both files must be stereo")

    delay = find_delay(src, rec)
    print(f"Round trip: {delay} frames ({delay / RATE * 1000:.2f} ms), "
          "including the computer's own buffering")

    # Mismatches everywhere usually mean the level or format changed on the
    # way, not a USB problem, so check that first
    a = src.read(RATE, 2 * RATE)
    b = rec.read(RATE + delay, 2 * RATE + delay)
    if len(a) == len(b) and np.mean(np.any(a != b, axis=1)) > 0.5:
        a, b = a.astype(np.float64), b.astype(np.float64)
        gain = np.sum(a * b) / np.sum(a * a)
        sys.exit(f"FAIL: the recording doesn't match the source at all. Its "
                 f"level is {gain:.6f} x the source's. Check that the "
                 "computer's output volume is at maximum, and that nothing "
                 "is resampling, dithering or applying gain.")

    # Walk the source in blocks. On a mismatch, look for a nearby offset that
    # matches again (a slip); if there is none, the samples were corrupted.
    pos, offset = 0, delay
    events, checked = [], 0
    while pos + BLOCK <= src.frames and pos + offset + BLOCK <= rec.frames:
        a = src.read(pos, pos + BLOCK)
        b = rec.read(pos + offset, pos + offset + BLOCK)
        bad = np.nonzero(np.any(a != b, axis=1))[0]
        if len(bad) == 0:
            pos += BLOCK
            checked += BLOCK
            continue

        first = pos + int(bad[0])
        resync = None
        guard = first + MAX_SLIP + 1  # look past the damaged region
        a2 = src.read(guard, guard + BLOCK)
        base = guard + offset - MAX_SLIP
        wide = rec.read(base, base + 2 * MAX_SLIP + BLOCK)
        for d in sorted(range(-MAX_SLIP, MAX_SLIP + 1), key=abs):
            b2 = wide[d + MAX_SLIP:d + MAX_SLIP + len(a2)]
            if base >= 0 and len(b2) == len(a2) and np.array_equal(a2, b2):
                resync = d
                break

        t = first / RATE
        if resync is None:
            events.append(f"{t:10.3f} s  samples differ (no resync found)")
        elif resync == 0:
            n = len(bad)
            events.append(f"{t:10.3f} s  {n} corrupted frame(s)")
        else:
            kind = "dropped" if resync < 0 else "repeated or inserted"
            events.append(f"{t:10.3f} s  slip: {abs(resync)} frame(s) "
                          f"{kind}")
            offset += resync
        checked += guard - pos
        pos = guard

    print(f"Checked {checked / RATE:.1f} s of {src.frames / RATE:.1f} s")
    if not events:
        print("PASS: every sample matched")
        return

    print(f"FAIL: {len(events)} problem(s)")
    for e in events[:50]:
        print(e)
    if len(events) > 50:
        print(f"... and {len(events) - 50} more")

    sys.exit(1)


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("make-signal", help="write a noise test file")
    m.add_argument("path")
    m.add_argument("--minutes", type=float, default=30)
    c = sub.add_parser("compare", help="compare a recording with the source")
    c.add_argument("source")
    c.add_argument("recording")
    args = p.parse_args()
    if args.cmd == "make-signal":
        make_signal(args.path, args.minutes)
    else:
        compare(args.source, args.recording)


if __name__ == "__main__":
    main()

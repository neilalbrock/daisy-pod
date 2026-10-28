# Wavetables from AKWF

Could [AKWF-FREE](https://github.com/KristofferKarlAxelEkstrand/AKWF-FREE)'s
single-cycle waveforms make interesting wavebyte tables? Findings from
surveying the whole library, and a prototype converter,
[`tools/akwf_tables.py`](../tools/akwf_tables.py).

**Short answer:** yes, and they suit wavebyte's format well. Almost every
AKWF wave survives the reduction to 128 samples and 8 bits. The licence
is CC0. The catch is that AKWF is a library of separate waves, not of
sweeps: in file order, neighbouring waves are nearly as unlike each other
as two picked at random, so a table made by stacking a folder's waves
jumps from sound to sound. The work is in ordering and blending them,
which the prototype does offline. Into the firmware, the cheapest route is
a second bank of tables (64-127), built at boot from a few key waves each,
as the generated tables are now.

## What AKWF is

- 4,358 waves in 64 folders under `AKWF/`, each one cycle of 600 samples,
  16-bit, mono. Some folders are instruments sampled one cycle at a time
  (cello, violin, oboe, flute, sax, piano, e-piano, organ, guitars, basses,
  human voice, birds); others are synth shapes (band-limited saws, squares
  and triangles in many variants, FM, chip and video game waves, bit
  reduction, hand-drawn, granular snippets); and `AKWF_0001`-`0020` are
  2,000 miscellaneous waves.
- The `AKWF--` folders are the same waves converted for other instruments
  (Surge, Teensy, Elektron, Synthesis Technology...). They aren't samples
  of those products. `AKWF--Synthesis-Technology/Wavetables` has 71
  ready-made tables of 64 waves x 256 samples, the same shape as
  wavebyte's, but each is just 64 consecutive files, so they have the
  ordering problem below.
- **Licence:** CC0 (public domain). No attribution needed, though crediting
  it in the README would be nice.

## Do they fit 128 samples and 8 bits?

A 128-sample wave holds harmonics 1 to 63, so each 600-sample cycle is
resampled by keeping those and dropping the rest (an FFT, not
interpolation, so nothing aliases). Over all 4,358 waves:

| | |
|---|---|
| Energy above harmonic 63, median | 0.006% |
| Waves losing more than 1% (-20 dB) | 569 (13%) |
| Waves losing more than 10% | 52 (1.2%), mostly bit-reduced, chip and granular |
| 8-bit quantisation, median signal to noise | 47 dB (min 32 dB) |

So 128 samples keep almost all of almost every wave, and 8 bits gives the
same grain as the firmware's own tables. The bright, buzzy waves lose some
top end, which the 8-bit oscillator's aliasing then puts back in its own
way.

## The catch: order

How alike neighbouring waves are, measured as the distance between their
harmonic spectra (each normalised: 0 is the same, 1.41 nothing in
common):

| | Neighbours |
|---|---|
| The firmware's 64 generated tables (median) | 0.09 |
| Two AKWF waves picked at random | 0.75 |
| Synth families in file order (`bw_sin`, `bw_saw`, `bw_squ`, `vgamebasic`...) | 0.02-0.24 |
| Instrument folders in file order | 0.12 (`flute`) to 0.70 (`piano`); most above 0.35 |
| Miscellaneous folders `0001`-`0020` in file order | 0.42-0.69 |

Only the folders made as families (the band-limited shapes, the basic chip
waves) are sweeps already. The instrument folders are sets of cycles taken
from different notes and moments, so stepping through one in file order
changes the timbre at random. That can be a sound (the PPG and Microwave
had jumpy tables too), but it isn't what a wave envelope or LFO sweep
wants.

## Making tables

`tools/akwf_tables.py` (numpy only) makes 61-wave tables, rotating each
wave to line up with the one before (the shift that correlates best), so
Clean mode's crossfades don't cancel and 8-bit steps are small, then
storing them as `Store()` does: DC out, peak at full scale, rounded to 8
bits. It has five ways to build a table:

- **Path:** every wave in one or more folders, ordered greedily so each
  is the closest remaining to the one before, starting from the darkest.
  On its own, a long folder still steps noticeably (the voice folder:
  0.45), as its waves are far apart. Taking 12-16 key waves along the
  path and crossfading between them, as the Microwave filled its tables
  between key waves, gives smooth sweeps (voice: 0.14; six orchestral
  folders together: 0.16).
- **Keys:** a hand-picked list of waves, crossfaded, for a designed sweep
  (double bass, electric bass, clavinet, sax, guitar: 0.06).
- **Families on a sampled wave:** one AKWF wave through one of the
  firmware's own families: hard sync, folding, the wave minus a shifted
  copy of itself (PWM for any shape), squeezing the cycle into a narrowing
  window (formant-like). The table starts as the instrument and turns into
  something synthetic. This is the most "wavebyte" of the ideas: the
  existing recipes, with an AKWF wave in place of the sine, triangle,
  square or saw they start from.
- **Raw:** a folder in file order, for comparison.

The demos are each an 8-second sweep from wave 0 to 60 and back at C3,
played as the 8-bit oscillator plays (nearest sample, the wave stepping
once per 16-sample block), with no filter, envelope or DAC model. Two are
also played in Clean mode.

| Demo | Made by | Neighbours |
|---|---|---|
| AKWF Voice | `hvoice`, 12 keys along the path | 0.14 |
| AKWF Voice Steps | `hvoice`, the whole path | 0.45 |
| AKWF Orchestra | cello, violin, sax, oboe, clarinet, flute: 16 keys | 0.16 |
| AKWF EPiano | `epiano`, the whole path | 0.13 |
| AKWF FM | `fmsynth`, 16 keys | 0.23 |
| AKWF Chip | `vgamebasic`, the whole path | 0.05 |
| Sync Cello | one cello cycle, synced 1-8x | 0.46 |
| Fold Voice | one voice cycle, folded | 0.04 |
| Self PWM Organ | one organ cycle minus itself, shifted | 0.23 |
| Squeeze Sax | one sax cycle, squeezed | 0.07 |
| Bass to Brass | five hand-picked keys | 0.06 |
| Unordered 0001 | `AKWF_0001`'s first 61 files, as they come | 0.63 |

Run it to hear them (they aren't in the repository):

```sh
git clone --depth 1 https://github.com/KristofferKarlAxelEkstrand/AKWF-FREE ../AKWF-FREE
python3 apps/wavebyte/tools/akwf_tables.py ../AKWF-FREE build/akwf --wav
```

It also writes `akwf_tables.h`, the tables as `int8_t` arrays, which
nothing reads yet.

## Into the firmware

The oscillators don't care where a table came from: an AKWF table costs
exactly the CPU a generated one does. The questions are where the data
lives and how a table is picked.

- **Where they go:** a second bank, tables 64-127, beside the generated
  ones rather than replacing them. That's another 512 KB of SDRAM
  (`wave_memory` doubles, out of 64 MB). The CC map's table CCs (3 and 9)
  already send 0-127, presets store the table's number (0-63 keep their
  meaning), and the web page's menus take the names. On the Pod, 128
  steps of the encoder is a lot: Shift and the encoder turn could jump by
  bank, or by 8.
- **Option A, whole tables in the image:** 7.8 KB each, as `const`
  data. The image runs from SRAM (480 KB, 172 KB used), so 16-24 tables
  would fit without crowding stages 5 and 6, but not 64.
- **Option B, key waves in the image, built at boot (recommended):** 12-16
  key waves per table (1.5-2 KB) plus a crossfade at boot into SDRAM,
  the way `kRandom` builds its tables from six keys now. 64 tables come
  to 100-130 KB of SRAM. The ordering, alignment and choice of keys
  happen offline in the tool, which would write a `.cpp` of key waves; the
  firmware gains a recipe family "keys", and another for the families on
  a sampled wave (sync, fold, PWM, squeeze), whose sources are 128-sample
  waves at 128 bytes each.
- **Option C, upload from the web page (stage 7):** the page reads WAV
  files (AKWF's or anyone's), runs the same steps in JavaScript, and sends
  the tables over SysEx (about 9 KB each). The firmware writes them to
  its own region of the QSPI flash, clear of the bootloader's copy of the
  app and the presets' last 1 MB, and copies them to SDRAM at boot. This
  is the most flexible, and the most work: flash writes have to go through
  the main loop as presets' do.

B first, then C if it's wanted: B's tool and recipes are the pipeline C
would run in the browser.

## Caveats

- A single cycle is a still photograph of an instrument: it has the
  timbre but none of the motion. The cello table won't sound like a
  cello. Movement comes from sweeping (wave envelope, LFOs, Flavour), so
  ordering matters more than which waves are chosen.
- Every wave is normalised to full scale, as the firmware's are, so a
  sweep holds its level even where the source waves were quiet.
- The prototype's choices (greedy ordering, the number of keys, which
  single waves to put through the families) were picked by the numbers,
  not by ear. A good bank of 64 needs listening, ideally through the
  Pod's filter and DAC model rather than the bare oscillator the demos
  use.

# Wavetables from AKWF

Could [AKWF-FREE](https://github.com/KristofferKarlAxelEkstrand/AKWF-FREE)'s
single-cycle waveforms make interesting wavebyte tables? Findings from
surveying the whole library, and the converter that makes wavebyte's AKWF
bank from them, [`tools/akwf_tables.py`](../tools/akwf_tables.py).

**Short answer:** yes, and they suit wavebyte's format well. Almost every
AKWF wave survives the reduction to 128 samples and 8 bits. The licence
is CC0. The catch is that AKWF is a library of separate waves, not of
sweeps: in file order, neighbouring waves are nearly as unlike each other
as two picked at random, so a table made by stacking a folder's waves
jumps from sound to sound. The work is in ordering and blending them,
which the converter does offline. Wavebyte now has them as a second bank
of 64 tables (64-127), built at boot from key waves or a source wave each,
as its own tables are: see [The bank](#the-bank).

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

## The bank

What came of this: wavebyte's second bank, tables 64-127, picked per
oscillator with Shift and the encoder turn on an Osc page, or CC 12 and
13 (see the [README](../README.md#the-akwf-bank)). The firmware's image
runs from SRAM (480 KB), so it can't carry 64 whole tables (500 KB).
`tools/akwf_tables.py` writes `akwf_bank.{h,cpp}` instead: a recipe per
table and the waves it needs, 831 of them (103 KB), and
`wavetables.cpp` builds the tables from them at power-up, into SDRAM
beside its own. The tool does the same builds (`build_keys`,
`build_family`) for its previews; on the host, the firmware's tables
match them to within a rounding step.

Two kinds of recipe:

- **Keys:** key waves crossfaded across the table's 61 waves, as the
  Microwave filled its tables between key waves. The keys come from a
  path through one or more folders (every wave, ordered greedily so each
  is the closest remaining to the one before, starting from the darkest,
  then 12 or 16 taken evenly along it), or are picked by hand. On its own,
  a long folder steps noticeably from wave to wave (the voice folder:
  0.45), as its waves are far apart; crossfading a dozen keys gives smooth
  sweeps (voice: 0.14; six orchestral folders together: 0.16). Four
  tables (Voice Step, EPiano, FM Steps, Chip) keep 61 waves along the path
  and step through them.
- **Families on a sampled wave:** one AKWF wave through hard sync (the
  cycle restarted 1 to 8 times a period), folding (gain into a triangle
  folder), PWM (the wave minus a copy of itself shifted by up to half a
  cycle) or squeezing (the cycle in a window narrowing to 15%,
  formant-like). The table starts as the instrument and turns into
  something synthetic: the firmware's own recipes, with an AKWF wave in
  place of the sine, triangle, square or saw they start from. Each wave is
  worked out at 512 points and band-limited back to 63 harmonics (an FFT,
  at boot), so unlike our own families they don't alias in the table.

Every key is rotated to line up with the one before (the shift that
correlates best), so crossfades don't cancel and 8-bit steps are small,
and every wave is stored as `Store()` does: DC out, peak at full scale,
rounded to 8 bits. The 64 were chosen by the numbers: 20 voices and
instruments, 26 synth shapes and miscellaneous folders, 18 families. The
names stay within 10 characters, since the web page gets all 65 options
(Off first) in one 720-byte message; the tool checks.

To change the bank, edit `BANK` in the tool and run it, with AKWF-FREE
cloned somewhere (only its `AKWF/` folder is read, about 36 MB with a
sparse checkout):

```sh
git clone --depth 1 --filter=blob:none --sparse https://github.com/KristofferKarlAxelEkstrand/AKWF-FREE ../AKWF-FREE
git -C ../AKWF-FREE sparse-checkout set AKWF
uv run python apps/wavebyte/tools/akwf_tables.py ../AKWF-FREE --wav build/akwf
```

It rewrites `akwf_bank.{h,cpp}` and, with `--wav`, renders a 6-second
sweep from wave 0 to 60 and back at C3 through each table, played as the
8-bit oscillator plays it (nearest sample, the wave stepping once per
16-sample block), with no filter, envelope or DAC model. It prints how
smooth each table is (the neighbours measure above).

Still to come, if it's wanted: uploading tables from the web page (stage
7), with the same steps in JavaScript, written to the app's own region of
the QSPI flash.

## Caveats

- A single cycle is a still photograph of an instrument: it has the
  timbre but none of the motion. The cello table won't sound like a
  cello. Movement comes from sweeping (wave envelope, LFOs, Flavour), so
  ordering matters more than which waves are chosen.
- Every wave is normalised to full scale, as the firmware's are, so a
  sweep holds its level even where the source waves were quiet.
- The bank's choices (greedy ordering, the number of keys, which single
  waves to put through the families) were picked by the numbers, not by
  ear. It wants a listening pass on the Pod, through the filter and DAC
  model rather than the bare oscillator the previews use, then pruning.

# Daisy Pod experiments

Firmware experiments for the Electrosmith Daisy Pod (Daisy Seed 3). Each one is
a separate app under `apps/`, all built from `main`.

| App | Description |
|---|---|
| [grainfall](apps/grainfall/README.md) | Granular and micro-loop effects after the Hologram Microcosm, with tap tempo, MIDI clock and the Microcosm's MIDI map (default) |
| [driftnet](apps/driftnet/README.md) | Always-listening micro-looper and spatial effect after Chase Bliss's MOOD MKII, with a CLOCK that sets the sample rate of both, MOOD's MIDI map, hidden options, ramping and Synth Mode |
| [tapeworn](apps/tapeworn/README.md) | Lo-fi tape machine: wow, flutter, hysteresis saturation, tape models, failures |
| [slowdrift](apps/slowdrift/README.md) | Stereo ambient reverb after Walrus Audio's Slöer: a modulated plate with five modes (shimmer, octave down, echoes, swells, a frozen pad), Stretch, which lowers its sample rate, and a MIDI CC map |
| [starter](apps/starter/README.md) | Low-pass filter toolchain and hardware check; template for new apps |

Code shared between apps goes in `common/`: `MidiClock` and `Tempo` (tap tempo or MIDI clock), `ClockDomain` (a
second, variable sample rate), `OnsetDetector` (note onsets), `Plate` (a Dattorro plate reverb, the
reverb in grainfall and slowdrift) and the `PitchShift` octave shifter it uses, `PagedKnobs`,
`PressHold` (tap vs hold on a button), `BypassFade`, `Limiter`, `SampleBuffer`
(a stereo ring for SDRAM), `Hue()` for LED colours, the DSP helpers in `dsp.h` (`Rng`, `Biquad`, `Svf`, `DcBlock` and
friends), the web link protocol (`web_link.h`), and `Presets` (`presets.h`: eight presets and the
global settings, saved to the QSPI flash through `PresetStore` and `QspiFlash`).

Every app but the starter has eight presets, kept in flash so they survive
power-off and reflashing: a preset page on the Pod (the last page; turn the
encoder to select a slot, long-press to save), program changes, and the web
page. Global settings like mono in are saved as you set them. For a factory
reset (one app's presets and settings), hold both buttons while powering up.
See each app's README.

## Web control surface

`web/` is a page that connects to the Pod over USB MIDI and shows what the
app is doing (for grainfall, the buffer with every voice and delay tap, the
beat grid and note onsets; for tapeworn, tape speed, dropouts and the
saturator's hysteresis loop; for driftnet, the loop and what's playing it,
with its clock and Env's detector; for slowdrift, the LFO, swells and decay
time; for all, levels and CPU) next to all its controls. Run `make web` and
open http://localhost:8000 in Chrome or Edge. Grainfall, tapeworn, driftnet
and slowdrift support it; see [web/README.md](web/README.md).

## Toolchain

| Tool | Version | Location |
|---|---|---|
| Arm GNU Toolchain (`arm-none-eabi-gcc`) | 15.3.rel1 | `~/toolchains/arm-gnu-toolchain-15.3.rel1-darwin-arm64-arm-none-eabi` (on `PATH` via `~/.zshrc`) |
| dfu-util | 0.11 | Homebrew |
| OpenOCD | 0.12.0 | Homebrew (only needed with an ST-Link debug probe) |
| libDaisy | v9.0.0 | `lib/libDaisy` (git submodule) |
| DaisySP | master | `lib/DaisySP` (git submodule) |

## First-time setup (fresh clone)

```sh
git submodule update --init --recursive
make libs        # builds libDaisy + DaisySP
```

## Build

Choose an app with `APP=<name>`. Without it, `make` builds the default set at
the top of the `Makefile` (currently `grainfall`).

```sh
make APP=starter     # outputs build/starter/starter.{bin,elf,hex}
make list            # list the apps; * marks the default
make all-apps        # build every app (do this after changing common/)
make clean           # remove one app's build (also takes APP=)
make clean-all       # remove all builds
make new APP=reverb  # start apps/reverb/ from a copy of the starter
```

Every `.cpp` file in the app's folder and in `common/` is compiled, so new
files need no Makefile changes. Object files are named after the source file
alone, so file names must be unique across the app and `common/`.

## Flash over USB (no debug probe)

Apps run from SRAM, loaded by the Daisy bootloader (`APP_TYPE = BOOT_SRAM`).
That lifts the 128 KB internal flash limit, and switching apps never touches
the bootloader.

**Once per Seed**, install the bootloader:

1. Connect the Seed's USB-C port to your Mac.
2. Put the Seed in the chip's own DFU mode: hold **BOOT**, tap **RESET**,
   release **BOOT**.
3. `make program-boot`

**To flash an app:**

1. Tap **RESET**. The Seed's LED pulses for about 2.5 s while the bootloader
   waits for DFU. Press **BOOT** during that time to keep it waiting (the LED
   blinks quickly to confirm). With no app installed yet, it waits anyway.
2. `make APP=<name> program-dfu` (builds first if needed)

`dfu-util` may print `Error during download get_status` at the end. This is
expected, because the board resets as soon as flashing finishes.

## Flash and debug with an ST-Link (optional)

The Seed 3 has through-hole pads for the SWD debug header (an STLINK-V3MINIE
is the usual probe).

```sh
make APP=<name> program   # flash via OpenOCD (BOOT_NONE apps only)
make openocd              # terminal 1: start the GDB server
make APP=<name> debug     # terminal 2: attach arm-none-eabi-gdb
```

libDaisy's `program` rule refuses bootloader apps. To flash one with the
probe, set `APP_TYPE = BOOT_NONE` in its `app.mk` (it must fit in 128 KB),
which overwrites the bootloader.

Build with `make DEBUG=1 OPT=-Og` for a debuggable binary. Run `make clean`
first so every object is rebuilt with the new flags.

## Memory notes

- With `BOOT_SRAM`, code goes in SRAM (480 KB) and globals in the 128 KB
  DTCM. The build prints both. Grainfall uses most of its DTCM, largely for
  libDaisy's MIDI handlers, and the stack shares what's left, so keep new
  state small and buffers in SDRAM.
- An app can go back to the internal flash with `APP_TYPE = BOOT_NONE` in
  `apps/<name>/app.mk`, but flashing it overwrites the bootloader.
- Large audio buffers (grain/delay buffers) belong in the 64 MB SDRAM:
  `float DSY_SDRAM_BSS buffer[48000 * 10];`
- Buffers read many times a sample (the reverbs in grainfall, driftnet and slowdrift) go
  in the 256 KB of RAM_D2, which reads faster, through the linker script's
  `.heap` section. Leave room there: malloc's heap starts where it ends.
- Presets live in the last 1 MB of the 8 MB QSPI flash (from 0x700000), 64 KB
  per app. The bootloader keeps its first 256 KB to itself and the app image
  at 0x40000, so flashing an app never reaches them.


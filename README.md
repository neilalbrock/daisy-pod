# Grainfall

Effects processor for the Electrosmith Daisy Pod (Daisy Seed 3).

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

```sh
make             # outputs build/grainfall.bin / .elf / .hex
make clean
```

## Flash over USB (no debug probe)

1. Connect the Seed's USB-C port to your Mac.
2. Put the Seed in DFU mode: hold **BOOT**, tap **RESET**, release **BOOT**.
3. `make program-dfu`

`dfu-util` may print `Error during download get_status` at the end. This is
expected, because the board resets as soon as flashing finishes.

## Flash and debug with an ST-Link (optional)

The Seed 3 has through-hole pads for the SWD debug header (an STLINK-V3MINIE
is the usual probe).

```sh
make program     # flash via OpenOCD
make openocd     # terminal 1: start the GDB server
make debug       # terminal 2: attach arm-none-eabi-gdb
```

Build with `make DEBUG=1 OPT=-Og` for a debuggable binary. Run `make clean`
first so every object is rebuilt with the new flags.

## Memory notes

- Code goes in the 128 KB internal flash by default (`APP_TYPE = BOOT_NONE`).
  If the program outgrows it, set `APP_TYPE = BOOT_SRAM` in the Makefile,
  flash the Daisy bootloader once with `make program-boot`, then use
  `make program-dfu` as usual.
- Large audio buffers (grain/delay buffers) belong in the 64 MB SDRAM:
  `float DSY_SDRAM_BSS buffer[48000 * 10];`

## Pod controls (granular engine)

Input is recorded continuously into a 10 s buffer in SDRAM and played back as
a cloud of short, windowed grains. The two knobs are shared across three pages.
Press the encoder to change page. After a page change, a knob does nothing
until you move it, so values don't jump.

| Page (LED 2) | Knob 1 | Knob 2 |
|---|---|---|
| 1 (blue) | Grain size, 5 ms - 500 ms | Density, 1 - 100 grains/s |
| 2 (magenta) | Position: how far back grains start, 0 - 2 s | Spray: random extra offset, 0 - 1 s |
| 3 (yellow) | Dry/wet mix | Stereo spread |

| Control | Function |
|---|---|
| Encoder turn | Pitch in semitones (-24 to +24) |
| Encoder press | Next page |
| Button 1 | Bypass (LED 1: green = active, red = bypassed) |
| Button 2 | Freeze the buffer (LED 1 blue while frozen) |

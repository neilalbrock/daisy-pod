# Daisy Pod experiments

Firmware experiments for the Electrosmith Daisy Pod (Daisy Seed 3). Each one is
a separate app under `apps/`, all built from `main`.

| App | Description |
|---|---|
| [grainfall](apps/grainfall/README.md) | Granular processor with MIDI clock sync (default) |
| [tapeworn](apps/tapeworn/README.md) | Lo-fi tape machine: wow, flutter, hysteresis saturation, tape models, failures |
| [usbaudio](apps/usbaudio/README.md) | The Pod as a USB sound card; hardware check for `UsbAudio` |
| [starter](apps/starter/README.md) | Low-pass filter toolchain and hardware check; template for new apps |

Code shared between apps goes in `common/` (currently `MidiClock` and
`PagedKnobs`). `common/usb_device/` holds `UsbAudio`, which makes the Pod a
USB sound card. It's opt-in (see [USB audio](#usb-audio)).

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

1. Connect the Seed's USB-C port to your Mac.
2. Put the Seed in DFU mode: hold **BOOT**, tap **RESET**, release **BOOT**.
3. `make APP=<name> program-dfu` (builds first if needed)

`dfu-util` may print `Error during download get_status` at the end. This is
expected, because the board resets as soon as flashing finishes.

## Flash and debug with an ST-Link (optional)

The Seed 3 has through-hole pads for the SWD debug header (an STLINK-V3MINIE
is the usual probe).

```sh
make APP=<name> program   # flash via OpenOCD
make openocd              # terminal 1: start the GDB server
make APP=<name> debug     # terminal 2: attach arm-none-eabi-gdb
```

Build with `make DEBUG=1 OPT=-Og` for a debuggable binary. Run `make clean`
first so every object is rebuilt with the new flags.

## USB audio

`UsbAudio` (`common/usb_device/usb_audio.h`) makes the Seed's USB port a
class-compliant USB sound card: stereo in and out, 24-bit, 48 kHz, no driver
needed. To use it in an app:

1. Add `USB_DEVICE = 1` to `apps/<name>/app.mk`.
2. In `main()`, call `usb.Init("Name")` before `hw.StartAudio`.
3. In the audio callback, call `usb.Read(left, right, size)` for audio from
   the computer and `usb.Write(left, right, size)` for audio to it, once per
   block each.

It brings its own copy of ST's USB device stack, which replaces libDaisy's.
An app that uses it can't also use libDaisy's USB device classes
(`MidiUsbHandler`, USB logging, `UsbHandle`), and the link fails if it tries.
See `apps/usbaudio/` for an example.

## Memory notes

- Code goes in the 128 KB internal flash by default (`APP_TYPE = BOOT_NONE`).
  If an app outgrows it, add `APP_TYPE = BOOT_SRAM` to `apps/<name>/app.mk`,
  flash the Daisy bootloader once with `make program-boot`, then use
  `make program-dfu` as usual. Flashing a `BOOT_NONE` app afterwards
  overwrites the bootloader, so switching back to a `BOOT_SRAM` app means
  running `make program-boot` again.
- Large audio buffers (grain/delay buffers) belong in the 64 MB SDRAM:
  `float DSY_SDRAM_BSS buffer[48000 * 10];`


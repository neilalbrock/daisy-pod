# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Bare-metal firmware experiments for the Electrosmith Daisy Pod (Daisy Seed 3, STM32H750), built with libDaisy (hardware abstraction) and DaisySP (DSP building blocks). Both are vendored as git submodules under `lib/`. Don't edit the submodules.

Each experiment is a separate app in `apps/<name>/`, and all of them live on `main` (experiments are folders, not branches). Code shared between apps goes in `common/`. Current apps:

- `apps/grainfall/`: granular processor with MIDI clock sync. This is the default `APP`.
- `apps/tapeworn/`: lo-fi tape machine (after Chase Bliss Generation Loss MKII).
- `apps/usbaudio/`: the Pod as a USB sound card, and the hardware check for `UsbAudio`.
- `apps/starter/`: low-pass filter hardware check, and the template that `make new` copies.

Each app has its own `README.md` with its control table. The top-level `README.md` indexes the apps.

## Commands

```sh
git submodule update --init --recursive   # fresh clone only
make libs            # build libDaisy + DaisySP (once after cloning or after bumping submodules)
make                 # build the default app -> build/grainfall/grainfall.{elf,bin,hex}
make APP=starter     # build another app -> build/starter/starter.{elf,bin,hex}
make all-apps        # build every app; run this after changing common/
make list            # list apps
make new APP=<name>  # copy apps/starter to apps/<name>
make clean           # remove one app's build dir; make clean-all removes build/
make DEBUG=1 OPT=-Og # debuggable build (run `make clean` first so all objects rebuild)

make APP=<name> program-dfu  # build if needed, then flash over USB; the Seed must be in DFU mode first (hold BOOT, tap RESET, release BOOT)
make APP=<name> program      # flash via ST-Link/OpenOCD
make openocd                 # GDB server (one terminal) ...
make APP=<name> debug        # ... then attach arm-none-eabi-gdb (another terminal)
```

There are no tests or linters. A successful build is the only check you can run locally. Use `make all-apps` when a change touches `common/`. Flashing needs the physical board, so leave it to the user unless they ask you to do it. At the end of a DFU flash, `dfu-util` prints `Error during download get_status`. This is expected, because the board resets as soon as flashing finishes.

Toolchain: Arm GNU Toolchain 15.3.rel1 (`arm-none-eabi-gcc`, on `PATH` via `~/.zshrc`), dfu-util and OpenOCD from Homebrew.

## Build system

The top-level `Makefile` sets a few variables, then includes `lib/libDaisy/core/Makefile`, which defines the build and flash targets. It adds `list`, `new`, `all-apps`, `clean-all` and `libs`. Override behaviour through those variables:

- `APP` selects the app (default `grainfall`). `TARGET` is the app name, and `BUILD_DIR` is forced to `build/$(APP)` with `override`, because libDaisy assigns it unconditionally.
- `CPP_SOURCES` is every `.cpp` in `apps/$(APP)/` plus every `.cpp` in `common/`, so new files need no Makefile edit. `common/` is on the include path. Object files are named by basename only, so file names must be unique across an app and `common/`.
- libDaisy's `program` and `debug` recipes hardcode `./build/$(TARGET).elf`. The Makefile symlinks that path to the app's ELF, so don't remove that rule.
- `apps/<name>/app.mk` (optional) holds per-app overrides and is included before libDaisy's Makefile. It should set variables only, not define rules.
- `USB_DEVICE = 1` in an app's `app.mk` adds `common/usb_device/` (not covered by the `common/*.cpp` wildcard) plus ST's USB device core sources from libDaisy's nested submodule, and puts `common/usb_device` first on the include path so its `usbd_conf.h` replaces libDaisy's.
- `CPP_STANDARD = -std=gnu++17` (libDaisy's default is gnu++14).
- `APP_TYPE` defaults to `BOOT_NONE`, which puts code in the 128 KB internal flash. If an app outgrows that, set `APP_TYPE = BOOT_SRAM` in its `app.mk`, flash the Daisy bootloader once with `make program-boot`, then keep using `make program-dfu`. Flashing a `BOOT_NONE` app overwrites the bootloader.
- `OCD_DIR` points at Homebrew's OpenOCD scripts.

## Firmware architecture

Every app's `main.cpp` follows the standard libDaisy pattern:

- `main()` initialises the `DaisyPod hw` global, sets the audio config (48 kHz, block size 4), initialises the DSP objects with `hw.AudioSampleRate()`, then calls `hw.StartAdc()` and `hw.StartAudio(AudioCallback)`.
- `AudioCallback` runs in interrupt context once per block. It calls `hw.ProcessAllControls()`, reads knobs and buttons (`GetKnobValue`, `button1.RisingEdge()`), then processes non-interleaved stereo (`in[ch][i]`, `out[ch][i]`). Keep it real-time safe: no heap allocation, no blocking calls, no `System::Delay`.
- The `while(1)` loop in `main()` handles slow, non-audio work: LEDs (`led1`/`led2.Set` followed by `hw.UpdateLeds()`).
- State shared between the callback and the main loop is held in plain globals.
- Apps with more parameters than knobs page them with `PagedKnobs` (`common/paged_knobs.h`): the encoder press calls `NextPage`, the callback calls `Update` each block, and `main()` calls `Latch` once before `StartAudio` so a knob does nothing until it moves.

USB audio: `common/usb_device/` makes the Seed's USB port a USB Audio Class 1 device (stereo in and out, 24-bit, 48 kHz). `usb_audio.{h,cpp}` holds the public `UsbAudio` class (`Init`, then `Read`/`Write` once per block in the audio callback), plus the descriptors, the class callbacks and the ring buffers. `usbd_conf.{h,c}` is the HAL glue, adapted from libDaisy's. libDaisy can't be used for this because it compiles the ST core with `USBD_MAX_NUM_INTERFACES 1`. Our objects take over from libDaisy's at link time because `usbd_conf.c` defines every global symbol that libDaisy's `usbd_conf.o` does; libDaisy's `system.o` references `hpcd_USB_OTG_HS`, so leaving any of them out drags libDaisy's copy back in. So an app with `USB_DEVICE = 1` can't also use libDaisy's USB device classes (`MidiUsbHandler`, USB logging, `UsbHandle`), and the link fails with a duplicate `OTG_FS_IRQHandler` if it tries. Clock drift between the codec (PLL3, about 48.014 kHz) and the computer is handled asynchronously: a feedback endpoint steers the computer's rate from the receive buffer's fill level, and outgoing packets are 47, 48 or 49 frames depending on the send buffer's level. The audio callback and the USB interrupt share only lock-free single-producer, single-consumer rings.

Memory: put large audio buffers (grain and delay lines) in the 64 MB external SDRAM with `DSY_SDRAM_BSS`, for example `float DSY_SDRAM_BSS buffer[48000 * 10];`. Internal RAM is small.

Grainfall: `apps/grainfall/main.cpp` holds the hardware setup and paged control handling. The granular DSP is in `apps/grainfall/grain_engine.{h,cpp}` (`GrainEngine`). `common/midi_clock.{h,cpp}` (`MidiClock`) follows incoming MIDI clock for tempo sync. MIDI (Pod TRS and USB) is polled in the audio callback so ticks are timestamped to within a block.

Tapeworn: `apps/tapeworn/main.cpp` holds the hardware setup and paged controls. `TapeEngine` (`tape_engine.{h,cpp}`) runs the chain, built from `Hysteresis` (Jiles-Atherton saturation, 2x oversampled), `Wow`/`Flutter` (`tape_wobble`), `TapeFailure` and `TapeModel` (EQ profiles). The DSP files don't include libDaisy, so they also compile on the host for offline tests.

If you change an app's control mapping, update the control table in that app's `README.md` and the header comment in its `main.cpp`. When adding an app, add it to the app table in the top-level `README.md`.

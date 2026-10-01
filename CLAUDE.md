# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Grainfall is bare-metal firmware for an audio effects processor on the Electrosmith Daisy Pod (Daisy Seed 3, STM32H750). It is built with libDaisy (hardware abstraction) and DaisySP (DSP building blocks), both vendored as git submodules under `lib/`. Don't edit the submodules. Project code lives in `src/`.

## Commands

```sh
git submodule update --init --recursive   # fresh clone only
make libs            # build libDaisy + DaisySP (once after cloning or after bumping submodules)
make                 # build -> build/grainfall.{elf,bin,hex}
make clean
make DEBUG=1 OPT=-Og # debuggable build (run `make clean` first so all objects rebuild)

make program-dfu     # flash over USB; the Seed must be in DFU mode first (hold BOOT, tap RESET, release BOOT)
make program         # flash via ST-Link/OpenOCD
make openocd         # GDB server (one terminal) ...
make debug           # ... then attach arm-none-eabi-gdb (another terminal)
```

There are no tests or linters. `make` succeeding is the only check you can run locally. Flashing needs the physical board, so leave it to the user unless they ask you to do it. At the end of a DFU flash, `dfu-util` prints `Error during download get_status`. This is expected, because the board resets as soon as flashing finishes.

Toolchain: Arm GNU Toolchain 15.3.rel1 (`arm-none-eabi-gcc`, on `PATH` via `~/.zshrc`), dfu-util and OpenOCD from Homebrew.

## Build system

The top-level `Makefile` sets a few variables, then includes `lib/libDaisy/core/Makefile`, which defines every build and flash target. Override behaviour through those variables:

- `CPP_SOURCES` lists the source files explicitly. **Add every new `.cpp` file to it.**
- `CPP_STANDARD = -std=gnu++17` (libDaisy's default is gnu++14).
- `APP_TYPE` defaults to `BOOT_NONE`, which puts code in the 128 KB internal flash. If the binary outgrows that, set `APP_TYPE = BOOT_SRAM`, flash the Daisy bootloader once with `make program-boot`, then keep using `make program-dfu`.
- `OCD_DIR` points at Homebrew's OpenOCD scripts.

## Firmware architecture

`src/main.cpp` holds the hardware setup and control handling, following the standard libDaisy pattern. The granular DSP lives in `src/grain_engine.{h,cpp}` (`GrainEngine`). `src/midi_clock.{h,cpp}` (`MidiClock`) follows incoming MIDI clock for tempo sync. MIDI (Pod TRS and USB) is polled in the audio callback so ticks are timestamped to within a block.

- `main()` initialises the `DaisyPod hw` global, sets the audio config (48 kHz, block size 4), initialises the DSP objects with `hw.AudioSampleRate()`, then calls `hw.StartAdc()` and `hw.StartAudio(AudioCallback)`.
- `AudioCallback` runs in interrupt context once per block. It calls `hw.ProcessAllControls()`, reads knobs and buttons (`GetKnobValue`, `button1.RisingEdge()`), then processes non-interleaved stereo (`in[ch][i]`, `out[ch][i]`). Keep it real-time safe: no heap allocation, no blocking calls, no `System::Delay`.
- The `while(1)` loop in `main()` handles slow, non-audio work: LEDs (`led1`/`led2.Set` followed by `hw.UpdateLeds()`).
- State shared between the callback and the main loop is held in plain globals.

Memory: put large audio buffers (grain and delay lines) in the 64 MB external SDRAM with `DSY_SDRAM_BSS`, for example `float DSY_SDRAM_BSS buffer[48000 * 10];`. Internal RAM is small.

The current program is a granular processor; its paged control mapping is documented in `README.md` and the header comment in `main.cpp`. If you change the control mapping, update the control table in `README.md` and the header comment in `main.cpp`.

# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Bare-metal firmware experiments for the Electrosmith Daisy Pod (Daisy Seed 3, STM32H750), built with libDaisy (hardware abstraction) and DaisySP (DSP building blocks). Both are vendored as git submodules under `lib/`. Don't edit the submodules.

Each experiment is a separate app in `apps/<name>/`, and all of them live on `main` (experiments are folders, not branches). Code shared between apps goes in `common/`. Current apps:

- `apps/grainfall/`: granular and micro-loop effects after the Hologram Microcosm (work in progress). This is the default `APP`.
- `apps/tapeworn/`: lo-fi tape machine (after Chase Bliss Generation Loss MKII).
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

make program-boot            # once per Seed: install the Daisy bootloader (chip DFU mode: hold BOOT, tap RESET, release BOOT)
make APP=<name> program-dfu  # build if needed, then flash over USB into the bootloader's DFU window (tap RESET, then press BOOT while the LED pulses to hold it there)
make APP=<name> program      # flash via ST-Link/OpenOCD (BOOT_NONE apps only)
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
- `CPP_STANDARD = -std=gnu++17` (libDaisy's default is gnu++14).
- `APP_TYPE` defaults to `BOOT_SRAM` (set after `app.mk` is included, so an app can override it): the Daisy bootloader, installed once with `make program-boot`, loads code into SRAM (480 KB). Globals go in the 128 KB DTCM, shared with the stack. Grainfall uses about 110 KB of it, mostly libDaisy's MIDI handlers, so keep new state small and put buffers in SDRAM. An app with `APP_TYPE = BOOT_NONE` goes in the 128 KB internal flash and overwrites the bootloader.
- `OCD_DIR` points at Homebrew's OpenOCD scripts.

## Firmware architecture

Every app's `main.cpp` follows the standard libDaisy pattern:

- `main()` initialises the `DaisyPod hw` global, sets the audio config (48 kHz, block size 4), initialises the DSP objects with `hw.AudioSampleRate()`, then calls `hw.StartAdc()` and `hw.StartAudio(AudioCallback)`.
- `AudioCallback` runs in interrupt context once per block. It calls `hw.ProcessAllControls()`, reads knobs and buttons (`GetKnobValue`, `button1.RisingEdge()`), then processes non-interleaved stereo (`in[ch][i]`, `out[ch][i]`). Keep it real-time safe: no heap allocation, no blocking calls, no `System::Delay`.
- The `while(1)` loop in `main()` handles slow, non-audio work: LEDs (`led1`/`led2.Set` followed by `hw.UpdateLeds()`).
- State shared between the callback and the main loop is held in plain globals.
- Apps with more parameters than knobs page them with `PagedKnobs` (`common/paged_knobs.h`): the encoder press calls `NextPage`, the callback calls `Update` each block, and `main()` calls `Latch` once before `StartAudio` so a knob does nothing until it moves.
- Buttons that do one thing on a tap and another on a hold go through `PressHold` (`common/press_hold.h`). Bypass crossfades with `BypassFade`, after a `Limiter` on dry + wet (both in `common/`).
- DSP code doesn't include libDaisy, so it also compiles on the host for offline tests. Shared helpers (`Rng`, `Biquad`, `DcBlock`, `Clamp`, `SmoothCoef`, `FastSin`…) are in `common/dsp.h`, namespace `dsp`.

Memory: put large audio buffers (grain and delay lines) in the 64 MB external SDRAM with `DSY_SDRAM_BSS`, for example `float DSY_SDRAM_BSS buffer[48000 * 10];`. Internal RAM is small.

Grainfall: `apps/grainfall/main.cpp` holds the hardware setup (it runs the Seed at 480 MHz with `hw.Init(true)`; the others use the default 400 MHz), paged controls, buttons and the MIDI map (Microcosm's CCs and program changes). MIDI (Pod TRS and USB) is polled in the audio callback so clock ticks are timestamped to within a block. `GrainEngine` (`grain_engine.{h,cpp}`) runs the chain: `Hold` (`hold.h`) -> `SampleBuffer` (interleaved stereo, power-of-two, SDRAM) -> the current `Effect` -> `PostFx` (pitch mod, resonant low-pass, volume). Each effect (`effect.h`) is a scheduler that, once per block, starts and steers voices from the `VoicePool` (`voice.{h,cpp}`: slices of the buffer played as grains or loops, with envelope, rate, reverse, filter and bitcrush), using the tempo grid and onsets (`onset.h`). `Tempo` (`tempo.{h,cpp}`) combines tap tempo with `MidiClock` (`common/midi_clock.{h,cpp}`). Effects decide how much of their output is fed back into the buffer (`Effect::Feedback`). Mosaic, Seq and Glide are in `fx_micro_loop.{h,cpp}`; Haze, Tunnel and Strum in `fx_granules.{h,cpp}`; Pattern and Warp in `fx_multidelay.{h,cpp}`, which work per sample in `Effect::Process` with `TapReader` delay taps (plain, pitch-shifted or reversed) and their own feedback from the last tap. Effect slots without a real effect yet use the placeholder `FxCloud` (`fx_cloud.{h,cpp}`).

Tapeworn: `apps/tapeworn/main.cpp` holds the hardware setup and paged controls. `TapeEngine` (`tape_engine.{h,cpp}`) runs the chain, built from `Hysteresis` (Jiles-Atherton saturation, 2x oversampled), `Wow`/`Flutter` (`tape_wobble`), `TapeFailure` and `TapeModel` (EQ profiles).

If you change an app's control mapping, update the control table in that app's `README.md` and the header comment in its `main.cpp`. When adding an app, add it to the app table in the top-level `README.md`.

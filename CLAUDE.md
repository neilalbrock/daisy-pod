# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Bare-metal firmware experiments for the Electrosmith Daisy Pod (Daisy Seed 3, STM32H750), built with libDaisy (hardware abstraction) and DaisySP (DSP building blocks). Both are vendored as git submodules under `lib/`. Don't edit the submodules.

Each experiment is a separate app in `apps/<name>/`, and all of them live on `main` (experiments are folders, not branches). Code shared between apps goes in `common/`. Current apps:

- `apps/grainfall/`: granular and micro-loop effects after the Hologram Microcosm. This is the default `APP`.
- `apps/tapeworn/`: lo-fi tape machine (after Chase Bliss Generation Loss MKII).
- `apps/driftnet/`: always-listening micro-looper and spatial effect (after Chase Bliss MOOD MKII).
- `apps/starter/`: low-pass filter hardware check, and the template that `make new` copies.

Each app has its own `README.md` with its control table. The top-level `README.md` indexes the apps.

`web/` is a browser control surface and visualiser (plain HTML/JS modules, no build step, served by `make web` on localhost because Web MIDI needs a secure context). It talks to an app over USB MIDI SysEx with the protocol in `common/web_link.{h,cpp}`, mirrored in `web/js/protocol.js`: change both together. See `web/README.md`.

## Commands

```sh
git submodule update --init --recursive   # fresh clone only
make libs            # build libDaisy + DaisySP (once after cloning or after bumping submodules)
make web             # serve the web control surface (web/) on http://localhost:8000
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
- Buttons that do one thing on a tap and another on a hold go through `PressHold` (`common/press_hold.h`). Bypass crossfades with `BypassFade`, after a `Limiter` on dry + wet (both in `common/`). `SampleBuffer` (`common/sample_buffer.h`) is an interleaved stereo ring for SDRAM, power-of-two long.
- DSP code doesn't include libDaisy, so it also compiles on the host for offline tests. Shared helpers (`Rng`, `Biquad`, `DcBlock`, `Clamp`, `SmoothCoef`, `FastSin`…) are in `common/dsp.h`, namespace `dsp`.

Memory: put large audio buffers (grain and delay lines) in the 64 MB external SDRAM with `DSY_SDRAM_BSS`, for example `float DSY_SDRAM_BSS buffer[48000 * 10];`. Internal RAM is small. Apps that read SDRAM per sample call `TuneSdramTimings()` (`common/sdram_timing.h`) right after `hw.Init`: libDaisy's row timings are far slower than the SDRAM part needs, and with every voice and tap in a different row, almost every cache miss opens a new row. Even so, count SDRAM reads when designing an effect.

Grainfall: `apps/grainfall/main.cpp` holds the hardware setup (it runs the Seed at 480 MHz with `hw.Init(true)`, as tapeworn does; starter uses the default 400 MHz), paged controls, buttons and the MIDI map (Microcosm's CCs and program changes). MIDI (Pod TRS and USB) is polled in the audio callback so clock ticks are timestamped to within a block. `GrainEngine` (`grain_engine.{h,cpp}`) runs the chain: `Hold` (`hold.h`) -> `SampleBuffer` (interleaved stereo, power-of-two, SDRAM) -> the current `Effect` -> `PostFx` (pitch mod, resonant low-pass, volume). Each effect (`effect.h`) is a scheduler that, once per block, starts and steers voices from the `VoicePool` (`voice.{h,cpp}`: slices of the buffer played as grains or loops, with envelope, rate, reverse, filter and bitcrush), using the tempo grid and onsets (`onset.h`). `Tempo` (`common/tempo.{h,cpp}`) combines tap tempo with `MidiClock` (`common/midi_clock.{h,cpp}`). Effects decide how much of their output is fed back into the buffer (`Effect::Feedback`). Mosaic, Seq and Glide are in `fx_micro_loop.{h,cpp}`; Haze, Tunnel and Strum in `fx_granules.{h,cpp}`; Pattern and Warp in `fx_multidelay.{h,cpp}`, which work per sample in `Effect::Process` with `TapReader` delay taps (plain, pitch-shifted or reversed) and their own feedback from the last tap. The web page's view of grainfall (parameter list, hello and telemetry frames) is in `web_view.{h,cpp}`; `main.cpp` applies `set` commands in the audio callback, snapshots the engine there on request (`want_telemetry`) and sends from the main loop, which runs every 10 ms. Blocks, Interrupt and Arp are in `fx_glitch.{h,cpp}`; Interrupt passes the live input through its wet output in `Process` (scaled by `Params::through`, which `main.cpp` sets from Mix so dry plus pass-through adds up to unity) and ducks it during glitches, so at 100% Mix it truly interrupts.

Tapeworn: `apps/tapeworn/main.cpp` holds the hardware setup and paged controls, and USB MIDI for the web page only (SysEx, read in the audio callback; no CC map). Its web view (`web_view.{h,cpp}`) sends traces of tape speed and level and the saturator's X-Y loop, from `TapeEngine::GetProbe()`. `TapeEngine` (`tape_engine.{h,cpp}`) runs the chain, built from `Hysteresis` (Jiles-Atherton saturation, 2x oversampled), `Wow`/`Flutter` (`tape_wobble`), `TapeFailure` and `TapeModel` (EQ profiles).

Driftnet: `apps/driftnet/main.cpp` holds the hardware setup (480 MHz), three knob pages (one per MOOD column; the encoder turn sets that page's three-way toggle: wet mode, loop mode, routing), the two buttons (a press switches a channel on at once, a tap switches it off, a hold freezes or overdubs; LATCH makes holds toggle), MIDI on every channel for now (TRS and USB, polled in the audio callback) with MOOD's CC map in `HandleCc`, and the web link. All the control state (toggles, MOOD's hidden options and DIP switches, ramping, tempo and Synth Mode settings) is plain globals written only in the callback, by the Pod, `HandleCc` and the page's `SetParam`; nothing is saved. Each block it updates `Tempo` (`common/tempo.{h,cpp}`; TIME syncs only once `HasTempo()` or MIDI clock is followed, in note values scaled by the CC 53/54 clock divisions), `Synth` (`synth.{h,cpp}`: notes set the clock in semitones and gate the output) and `Ramp` (`ramp.{h,cpp}`: bounce, one-off ramps and expression move chosen knobs), then hands `DriftEngine::Params` to the engine. TRUE BYPASS (CC 55) applies only while both channels are off: `Params::listen` stops the looper recording and trails are dropped. `DriftEngine` (`drift_engine.{h,cpp}`) keeps the dry signal at 48 kHz and runs everything else in the CLOCK's domain (`clock_domain.h`): a phase accumulator ticks the core at 2k-64k (stepped, or any rate for SMOOTH and Synth Mode) on the input interpolated to the tick's instant, and interpolates the core's output back to 48 kHz. Every buffer counts core frames, so the clock sets loop and delay length and pitch. Each tick: `MicroLooper` (`micro_looper.{h,cpp}`, with its Env, Tape and Stretch players and its options) plays its loop, routing picks what the Wet Channel hears, the selected wet mode runs (`WetReverb`, `WetDelay`, `WetSlip` in `wet_{reverb,delay,slip}.{h,cpp}`, each with its SPREAD and CLASSIC behaviour; only one runs, and a change fades out, calls the new mode's `Reset()` and fades in), then TONE and LEVEL BALANCE, and the looper either records input + wet into its ring (listening) or overdubs clean input at the play head. `Reset()` doesn't clear a ring: it counts frames written since, and older frames read as silence (clearing in the callback overruns: newlib-nano's `memset` goes a byte at a time). The Reverb's line and diffusers are in RAM_D2 (237 KB of its 256 KB, via the linker script's `.heap` section), because it reads 16 taps a tick and SDRAM reads are slow; keeping them out of DTCM also leaves room for the stack beside the two MIDI handlers. Don't fill RAM_D2: newlib's `_sbrk` grows the heap from the end of `.heap` with no limit check, and the USB stack mallocs, so running past the end of RAM_D2 hard-faults. The web view (`web_view.{h,cpp}`) sends the loop as a buffer section whose first head is the loop itself (marked as a tap, with the clock as its rate); the page draws it with `web/js/views/loop.js`, which `web/js/apps/driftnet.js` supplies as its `View`.

If you change an app's control mapping, update the control table in that app's `README.md` and the header comment in its `main.cpp`. When adding an app, add it to the app table in the top-level `README.md`.

# Architecture

## Directory layout

```
inmp441_rpi/
├── Makefile                  # build system (obj/ mirrors src/)
├── README.md
├── .gitignore
├── LICENSE
├── include/                  # public headers (*.hpp)
│   ├── core/
│   │   ├── Config.hpp        # CLI parsing/validation (incl. --alsa-device/--gpio-chip)
│   │   ├── Logger.hpp        # thread-safe stderr logger
│   │   └── SignalHandler.hpp # Ctrl+C / SIGTERM handling
│   └── audio/
│       ├── SampleFormat.hpp    # pure 24-bit <-> 16-bit / float conversions
│       ├── AlsaDeviceFinder.hpp# ALSA card lookup by name ("bare" overlay)
│       ├── INMP441.hpp         # microphone layer over ALSA (kernel I2S)
│       └── AudioProcessor.hpp  # RMS analysis, WAV writer, meter rendering
├── src/                      # implementation, one .cpp per header
│   ├── main.cpp
│   ├── core/
│   └── audio/
├── obj/                      # object files, tree mirrored from src/
├── bin/                      # final binary (inmp441_rpi)
├── docs/                     # documentation
├── scripts/                  # dependency installer and helpers
└── tests/                    # host-runnable unit tests (no hardware)
```

## Module responsibilities

```
                    ┌──────────────────────────────────────────────┐
                    │                 src/main.cpp                  │
                    │  parses Config, installs SignalHandler,      │
                    │  dispatches to the selected run mode         │
                    └──────┬──────────────┬───────────────┬────────┘
                           │              │               │
              ┌────────────▼───┐   ┌──────▼──────┐   ┌─────▼───────┐
              │  level meter   │   │  WAV record │   │ raw dump    │
              └───────┬────────┘   └──────┬──────┘   └─────┬───────┘
                      │                   │                │
                      └───────────────────▼────────────────┘
                                 ┌──────────────────┐
                                 │INMP441::Inmp441_t│  RAII handle, 24-bit AudioFrame
                                 └───────┬──────────┘
                                         │ raw 32-bit slots (S32_LE)
                                 ┌───────▼──────────┐
                                 │   ALSA capture   │  snd_pcm_open on plughw:<card>,0
                                 │ (kernel I2S drv, │  (auto-detected "bare" card);
                                 │ dtoverlay=inmp-  │  libgpiod drives the L/R
                                 │ 441-bare)        │  select line (GPIO21)
                                 └───────┬──────────┘
                                         ▼
                          Raspberry Pi I2S peripheral (kernel driver)
                          ┌─────────────────────────────────────────┐
                          │ BCLK (GPIO18)   WS (GPIO19)  SD (GPIO20)│
                          └─────────────────────────────────────────┘
                                         │
                                  ┌──────▼──────┐
                                  │   INMP441   │
                                  └─────────────┘
```

### Layers

1. **ALSA / kernel I2S driver** — the microphone is captured through the
   `dtoverlay=inmp441-bare` overlay (see `overlays/inmp441-bare.dts`), which
   exposes a `dmic-codec` sound card named "inmp441-bare". The capture stream
   is opened with `snd_pcm_open` on `plughw:<card>,0`; the card is
   auto-detected by name via `audio::FindAlsaDeviceByName`
   (`AlsaDeviceFinder.hpp` / `src/audio/AlsaDeviceFinder.cpp`, default
   "default", overridable with `--alsa-device`). The stream is S32_LE, 2
   channels, ~48 kHz, and the mic's 24 bits arrive MSB-aligned inside each
   32-bit slot. The L/R select line is driven with libgpiod on `gpiochip0`
   line 21 (overridable with `--gpio-chip`); LOW = left, HIGH = right.

2. **INMP441::Inmp441_t** — the microphone domain, and an RAII handle: the
   constructor opens the ALSA capture device (and selects the channel via the
   libgpiod L/R line) and throws `std::runtime_error` on failure; the
   destructor closes the PCM stream and releases the GPIO line, so owning it
   via `std::make_unique` releases everything at scope exit. Knows that the
   mic delivers 24-bit two's-complement data left-justified in 32-bit slots,
   one bit after the frame-sync edge (standard I2S). It converts raw slots
   into `AudioFrame { left24, right24 }` (see `SampleFormat.hpp`) and keeps
   the same public API: `readFrames()`, `resetRxStream()` (drop + prepare),
   `resetI2s()` (close and reopen the ALSA stream) and `readRawWords()`.

3. **AudioProcessor** — signal consumers:
   - `RmsAnalyzer` computes RMS / peak in dBFS for the meter.
   - `WaveWriter` writes a standard RIFF/WAVE 16-bit PCM file, patching header
     sizes on close.
   - `renderMeter` builds the ASCII level bar.

4. **core/** — cross-cutting concerns: `Logger` (stderr, timestamps, levels),
   `Config` (CLI parsing/validation, incl. `--alsa-device` / `--gpio-chip`),
   `SignalHandler` (cooperative shutdown).

## Design decisions

- **Kernel I2S via ALSA.** Capture goes through the kernel I2S driver
   (`dtoverlay=inmp441-bare`), resolved at build time with `pkg-config`
   (`libasound2-dev` + `libgpiod-dev`); `bcm2835` remains only for the OLED
   display. A plain Makefile keeps the toolchain footprint tiny.
- **stdout is reserved for data.** All diagnostics go to `stderr`, so
   `--dump` and future raw/PCM modes can be piped safely.
- **Mirrored object tree.** `obj/` replicates `src/` subdirectories
   (`src/audio/foo.cpp` → `obj/audio/foo.o`), keeping objects organised and
   collision-free.
- **Pure signal code is host-testable.** `SampleFormat.hpp` has no hardware
   dependencies, so `make test` runs the conversion unit tests on any machine.
- **ALSA PCM stream.** The kernel driver owns the I2S clocking (BCLK/WS/SD);
   reads are synchronous through the PCM stream and loss-free at the target
   rates (up to 48 kHz stereo). DMA/zero-copy is a possible future enhancement
   (see [testing.md](testing.md#future-work)).

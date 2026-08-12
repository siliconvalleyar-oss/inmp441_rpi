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
│   │   ├── Config.hpp        # command-line parsing results
│   │   ├── Logger.hpp        # thread-safe stderr logger
│   │   └── SignalHandler.hpp # Ctrl+C / SIGTERM handling
│   └── audio/
│       ├── SampleFormat.hpp  # pure 24-bit <-> 16-bit / float conversions
│       ├── I2SController.hpp # BCM2835 PCM/I2S peripheral driver
│       ├── INMP441.hpp       # microphone layer over the I2S transport
│       └── AudioProcessor.hpp# RMS analysis, WAV writer, meter rendering
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
                                 ┌─────────────────┐
                                 │INMP441::Inmp441_t│  RAII handle, 24-bit AudioFrame
                                 └───────┬─────────┘
                                         │ raw 32-bit slots
                                 ┌───────▼───────┐
                                 │I2SController  │  PCM/I2S + CM clock,
                                 │ (bcm2835)     │  GPIO 18/19/20/21
                                 └───────┬───────┘
                                         ▼
                          BCM2835 PCM/I2S peripheral (register access)
                          ┌─────────────────────────────────────────┐
                          │ BCLK (GPIO18)   WS (GPIO19)  SD (GPIO20)│
                          └─────────────────────────────────────────┘
                                         │
                                  ┌──────▼──────┐
                                  │   INMP441   │
                                  └─────────────┘
```

### Layers

1. **I2SController** — owns the BCM2835 PCM/I2S peripheral: configures the GPIO
   pin functions (ALT0), the PCM clock generator (`CM_PCMCTL` / `CM_PCMDIV`),
   the I2S master frame (MODE/RXC/TXC) and the RX FIFO access (`CS`/`FIFO`).
   Exposes raw 32-bit slots. See [i2s_registers.md](i2s_registers.md).

2. **INMP441::Inmp441_t** — the microphone domain, and an RAII handle: the
   constructor opens the I2S master and throws `std::runtime_error` on
   failure; the destructor shuts the hardware down, so owning it via
   `std::make_unique` releases everything at scope exit. Knows that the mic
   delivers 24-bit two's-complement data left-justified in 32-bit slots, one
   bit after the frame-sync edge (standard I2S). It converts raw slots into
   `AudioFrame { left24, right24 }` (see `SampleFormat.hpp`) and drives the
   L/R select line.

3. **AudioProcessor** — signal consumers:
   - `RmsAnalyzer` computes RMS / peak in dBFS for the meter.
   - `WaveWriter` writes a standard RIFF/WAVE 16-bit PCM file, patching header
     sizes on close.
   - `renderMeter` builds the ASCII level bar.

4. **core/** — cross-cutting concerns: `Logger` (stderr, timestamps, levels),
   `Config` (CLI parsing/validation), `SignalHandler` (cooperative shutdown).

## Design decisions

- **Zero external dependencies.** Only the `bcm2835` library plus the C++
   standard library. No ALSA, no cmake — a plain Makefile keeps the toolchain
   footprint tiny for a Zero 2 W.
- **stdout is reserved for data.** All diagnostics go to `stderr`, so
   `--dump` and future raw/PCM modes can be piped safely.
- **Mirrored object tree.** `obj/` replicates `src/` subdirectories
   (`src/audio/foo.cpp` → `obj/audio/foo.o`), keeping objects organised and
   collision-free.
- **Pure signal code is host-testable.** `SampleFormat.hpp` has no hardware
   dependencies, so `make test` runs the conversion unit tests on any machine.
- **Polled RX FIFO.** Reads are synchronous and loss-free at the target rates
   (up to 48 kHz stereo) on the Zero 2 W's 1 GHz CPU. DMA is a possible future
   enhancement (see [testing.md](testing.md#future-work)).

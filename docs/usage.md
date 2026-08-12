# Usage

```
Usage: inmp441_rpi [options]

Reads audio from an INMP441 MEMS microphone over the BCM2835 PCM/I2S
peripheral and prints a live level meter, records WAV, or dumps raw slots.

Options:
  -m, --mode <level|wav|dump>  Mode: level meter (default), WAV record,
                               or raw-slot dump.
      --wav [FILE]             Alias for --mode wav (default FILE=capture.wav).
      --dump [N]               Alias for --mode dump (default N=16 slots).
  -f, --file <FILE>            WAV output path (default: capture.wav).
  -r, --rate <HZ>              Sample rate: 8000,16000,32000,44100,48000
                               (default: 48000).
  -d, --duration <SEC>         Recording duration in seconds (default: 5).
  -c, --channel <left|right>   I2S channel the mic is on (default: left).
      --no-lr-gpio             Do NOT drive GPIO 21 (L/R pin). Use this if
                               L/R is hard-wired to GND (left) or 3.3 V (right).
  -v, --verbose                Verbose logging.
  -h, --help                   Show this help and exit.
      --version                Show version and exit.
```

## Modes

### `level` (default)

Shows a live ASCII meter: a 40-cell bar plus RMS and peak levels in dBFS,
refreshed several times per second. Press **Ctrl+C** to stop.

```
[=====-------------------------------]  RMS  -21.4 dBFS  PEAK  -12.1 dBFS
```

### `wav`

Records for `--duration` seconds (default 5) into a standard RIFF/WAVE file,
PCM **16-bit mono** by default. Use `--rate` for sample rate.

```bash
sudo ./bin/inmp441_rpi --wav mic.wav -d 10
sudo ./bin/inmp441_rpi --mode wav --file test.wav --rate 44100 --duration 5
```

### `dump`

Prints raw 32-bit I2S slots (pairs = one frame) and exits. Useful for
verifying wiring and sample alignment without ALSA.

```bash
sudo ./bin/inmp441_rpi --dump 32
```

Example output:

```
I2S raw slots (32-bit), 16 frames of 2 slots each
frame  0000  L 0x00A3F200  R 0x00000000
frame  0001  L 0xFF4B1000  R 0x00000000
```

## Sample alignment

The INMP441 delivers **24-bit left-justified** samples inside 32-bit slots.
The recovered 24-bit sample is the slot shifted right by 8:

```
raw    = 0x00A3F200   (slot as read from the FIFO)
24-bit = 0x00A3F2     (raw >> 8, arithmetic)
16-bit = 0x00A3       (raw >> 16)
```

When the mic is silent the slots read ≈ `0x00000000`; speaking produces values
across the full range. If your board deviates (e.g. a shift of one bit), open
`src/audio/I2SController.cpp` and adjust the I2S data-delay (`CHxPOS`) or the
conversion in `SampleFormat.hpp`, then rebuild.

## Notes

- All diagnostics go to **stderr**; `stdout` stays clean for data (dump mode).
- The `--channel` selection maps to the I2S **left** or **right** slot, which
  is determined by the microphone's **L/R** pin:
  - `left` → L/R driven LOW (GPIO 21 = 0) — default
  - `right` → L/R driven HIGH (GPIO 21 = 1)
  - `--no-lr-gpio` → GPIO 21 left untouched
- With a single mic on the left slot, the right slot reads ~0. The **right**
  slot is only meaningful if a second INMP441 with `L/R = HIGH` shares the bus.

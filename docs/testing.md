# Testing

## Unit tests (no hardware)

The pure signal-code lives in `include/audio/SampleFormat.hpp` and is
host-runnable, so conversion logic can be tested on any machine:

```bash
make test
```

Runs `tests/test_conversion.cpp`, which verifies:

- 24-bit sample recovery (`raw >> 8`) with positive and negative values
- 16-bit extraction (`raw >> 16`) and float normalisation
- sign extension of negative samples
- round trips within quantisation

## On-hardware validation

1. `sudo ./bin/inmp441_rpi --info` — confirms the board and requested
   configuration.
2. `sudo ./bin/inmp441_rpi --dump 64` — check the raw slots (see
   [usage.md](usage.md#sample-alignment)):
   - silence ⇒ slots ≈ `0x00000000`
   - clap ⇒ large signed swings in the left slot
3. `sudo ./bin/inmp441_rpi --level` — speak/clap; RMS should rise well above
   the noise floor.
4. Record a WAV and inspect it (e.g. `sox capture.wav -n stat`), or play it
   back to confirm it sounds correct.

## Expected values (quick reference)

| Input (raw slot) | recovered 24-bit | 16-bit | float |
| --- | --- | --- | --- |
| `0x00000000` | 0 | 0 | 0.0 |
| `0x80000000` | -8388608 (min) | -32768 | ≈ -1.0 |
| `0x7FFFFF00` | 8388352 (max) | 32767 | ≈ 1.0 |
| `0x00A3F200` | 41970 | 163 | ≈ 0.0050 |

## Future work

- DMA-backed capture (DREQ + `smi` or a zero-copy ring buffer) for
  higher/lossless data rates.
- Stereo with a second INMP441 (share BCLK/WS, `L/R = HIGH`).
- On-board self-test flag (`--selftest`) that drives a calibration tone.

# Usage

```
Usage: inmp441_rpi [options]

Reads audio from an INMP441 MEMS microphone over the BCM2835 PCM/I2S
peripheral. By default it shows a console presentation and an interactive
menu (duration/channel/format/record/level test). It can also run non-stop
modes: live level meter, WAV/MP3 recording, or raw-slot dump.

Options:
      --menu                 Interactive menu after a console presentation
                             (this is the default).
      --level                Live RMS/peak meter.
      --wav [FILE]           Record audio to a 16-bit PCM WAV file
                             (default: output/recording_YYYYMMDDHHMM.wav).
      --mp3 [FILE]           Record to a temp WAV then encode to MP3 with lame
                             (default: output/recording_YYYYMMDDHHMM.mp3).

When no file name is given, the default includes a local-time stamp down to
the minute, e.g. `output/recording_202608121137.wav`, so each recording gets
its own file. An explicit `--wav mic.wav` / `--mp3 test.mp3` is kept as-is.
      --dump [N]             Dump N raw 32-bit I2S words and exit (default 16).
      --info                 Print hardware/configuration info and exit.
  -r, --rate <HZ>            Sample rate: 8000,16000,32000,44100,48000
                             (default: 48000).
  -d, --duration <SEC>       Recording duration in seconds (default: 5, min 1).
      --warmup <SEC>         Seconds of audio discarded before recording
                             (default: 4; removes the I2S startup transient,
                             set 0 to disable).
      --gain <DB>            Digital gain applied to recordings (default 0).
                             The INMP441 is very quiet for speech: try +20 to
                             +30 dB for close talk, +40 dB for room ambience.
                             Clips at full scale.
      --dropout <SEC>        Flag runs of digital silence longer than this
                             (default 1 s) as mic dropouts in the recording
                             summary; use to diagnose flaky wiring/capsule.
  -c, --channel <left|right> I2S channel the mic is on (default: left).
      --meter                Show a live VU meter on stderr while recording.
                             The menu's RECORD option enables this automatically.
      --no-lr-gpio           Do NOT drive GPIO 21 (L/R pin). Use this if
                             L/R is hard-wired to GND (left) or 3.3 V (right).
      --config <FILE>        JSON configuration file to load/save
                             (default: config.json in the project directory).
      --save-config          Save the current settings to the config file
                             and continue. The interactive menu also saves
                             automatically on every change.
  -v, --verbose              Verbose logging.
  -h, --help                 Show this help and exit.
      --version              Show version and exit.
```

## Modes

### `menu` (default)

Shows a hardware presentation followed by an interactive menu. Options:
duration (min 5 s for test recordings by default), channel left/right,
output format WAV/MP3, a 5-second level test, and record.

```bash
sudo ./bin/inmp441_rpi
```

### `level`

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
sudo ./bin/inmp441_rpi --wav --file test.wav --rate 44100 --duration 5
```

### `mp3`

Same as `wav`, but transcodes with `lame` after recording.

```bash
sudo ./bin/inmp441_rpi --mp3 test.mp3 -d 5
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

## Persisted configuration

The application persists its settings to a JSON file (`config.json` by default,
overridable with `--config`):

- **Sample rate, channel, stereo, duration, warmup, gain, dropout threshold,
  meter interval and menu format (WAV/MP3)**.
- Settings are **loaded at startup** and used as defaults; command-line
  options always override the file.
- They are **saved** with `--save-config` or automatically every time an
  option is changed in the interactive menu.
- `config.json` is gitignored and is not part of the repository.

Example:

```json
{
  "sample_rate": 48000,
  "left_channel": true,
  "stereo": false,
  "duration_seconds": 10.0,
  "warmup_seconds": 4.0,
  "gain_db": 24.0,
  "dropout_seconds": 1.0,
  "meter_interval_ms": 120.0,
  "format": "wav"
}
```

```bash
# Save the current CLI settings
sudo ./bin/inmp441_rpi --gain 24 --save-config

# Use an alternative config file
sudo ./bin/inmp441_rpi --config /etc/inmp441_rpi.json --level
```

## Notes

- All diagnostics go to **stderr**; `stdout` stays clean for data (dump mode).
- The `--channel` selection maps to the I2S **left** or **right** slot, which
  is determined by the microphone's **L/R** pin:
  - `left` → L/R driven LOW (GPIO 21 = 0) — default
  - `right` → L/R driven HIGH (GPIO 21 = 1)
  - `--no-lr-gpio` → GPIO 21 left untouched
- With a single mic on the left slot, the right slot reads ~0. The **right**
  slot is only meaningful if a second INMP441 with `L/R = HIGH` shares the bus.

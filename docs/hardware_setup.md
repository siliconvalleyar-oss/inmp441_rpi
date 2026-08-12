# Hardware Setup

The INMP441 is a digital (I2S) MEMS microphone. It does **not** need an ADC —
it samples internally and streams 24-bit PCM over an I2S bus driven by the
Raspberry Pi in *master* mode.

## Pin map (as used by this project)

| INMP441 pin | Description            | Raspberry Pi (GPIO) | Physical pin |
| ----------- | ---------------------- | ------------------- | ------------ |
| VDD         | Power, 3.3 V           | 3.3 V               | 1 or 17      |
| GND         | Ground                 | GND                 | 6, 9, 14 ... |
| SD          | Data (PCM_DIN)         | GPIO 20             | 38           |
| SCK / BCLK  | Bit clock (PCM_CLK)    | GPIO 18             | 12           |
| WS / LRCK   | Word select (PCM_FS)   | GPIO 19             | 35           |
| L/R         | Channel select         | GPIO 21 (pin 40)    | 40           |

> L/R is wired to **physical pin 40** (GPIO 21). The driver drives that pin:
> `LOW` → the microphone transmits on the **left** I2S channel (default),
> `HIGH` → on the **right** channel. If you prefer to hard-wire L/R yourself,
> add `--no-lr-gpio` and connect L/R directly to GND (left) or 3.3 V (right).

## Wiring diagram

```
              Raspberry Pi Zero 2W                    INMP441
              ┌─────────────────┐                     ┌────────┐
  3.3 V ──────┤  Pin 1/17       │── VDD ─────────────┤ 1 VDD  │
  GND  ──────┤  Pin 6/9/14     │── GND ─────────────┤ 4 GND  │
  GPIO 18 ────┤  Pin 12 (PCM_CLK)│── SCK / BCLK ───────┤ 3 SCK  │
  GPIO 19 ────┤  Pin 35 (PCM_FS) │── WS  / LRCK ───────┤ 2 WS   │
  GPIO 20 ────┤  Pin 38 (PCM_DIN)│── SD  / DOUT ───────┤ 5 SD   │
  GPIO 21 ────┤  Pin 40          │── L/R ─────────────┤ 6 L/R  │
              └─────────────────┘                     └────────┘
```

## Before you start

1. **Remove I2S kernel overlays.** The userspace driver takes exclusive control
   of the PCM/I2S peripheral and its clock. Make sure no ALSA I2S overlay is
   active (`dtoverlay=googlevoicehat-soundcard`, `i2s-mems-mic`, etc.) in
   `/boot/config.txt`, otherwise the kernel and this program will fight over
   the same hardware. A fresh Raspberry Pi OS image works out of the box.
2. **Root access.** The `bcm2835` library needs `/dev/mem`. Run the binary
   with `sudo` (the `run` target and `scripts/run.sh` do this for you).
3. **Confirm the board.** Any of: Pi Zero, Pi Zero 2 W, Pi 1, Pi 2, Pi 3 or
   Pi 4 (BCM2835 family). This project targets the **Zero 2 W** (BCM2710 /
   BCM2837, 64-bit capable) and works on 32-bit and 64-bit OS images.

## Troubleshooting

| Symptom | Likely cause |
| --- | --- |
| No data (`RX FIFO timeout`) | BCLK/WS not reaching the mic; check GPIO 18/19/20 wiring |
| Signal present but very quiet or half-scale | Sample alignment: run `--dump` and confirm the 24-bit value is in bits 31–8 (see [usage.md](usage.md#sample-alignment)) |
| Left/right swapped | L/R selection differs from the `--channel` option |
| Constant noise / low amplitude | Check that the mic's L/R pin is not floating (hard-wire it or use the default GPIO 21 drive) |

# PCM/I2S Register Configuration

This document explains the low-level BCM2835 register programming performed by
`I2SController`. It is reference material; the implementation lives in
`src/audio/I2SController.cpp`.

The bit layouts below follow the **official Broadcom `bcm2835-i2s` kernel
driver** (`sound/soc/bcm/bcm2835-i2s.c`) and the **`bcm2835` kernel clock
driver** (`drivers/clk/bcm/clk-bcm2835.c`), which are the authoritative
sources for the actual silicon.

## Peripheral bases

| Peripherals block offset | Description |
| --- | --- |
| `0x203000` | PCM/I2S |
| `0x101000` | Clock manager (CM) |
| `0x101098` | `CM_PCMCTL` |
| `0x10109C` | `CM_PCMDIV` |

Access from userspace uses the `bcm2835_peripherals` pointer exposed by the
`bcm2835` library after `bcm2835_init()`:

```cpp
volatile uint32_t* reg =
    reinterpret_cast<volatile uint32_t*>(
        reinterpret_cast<uint8_t*>(bcm2835_peripherals) + 0x203000 + offset);
bcm2835_peri_write(reg, value);
```

## GPIO routing

| GPIO | Pin function | ALT code |
| --- | --- | --- |
| 18 | PCM_CLK → SCK / BCLK | ALT0 |
| 19 | PCM_FS → WS / LRCK | ALT0 |
| 20 | PCM_DIN → SD | ALT0 |
| 21 | L/R select (driven as output) | — |

## Clock generation

The PCM controller generates BCLK and WS in master mode from the PCM clock
(`CM_PCM`). BCLK must equal `sample_rate × 64` (2 × 32-bit slots per frame):

| Sample rate | BCLK | Divider (19.2 MHz xosc) |
| --- | --- | --- |
| 48 kHz | 3.072 MHz | 6.25 |
| 44.1 kHz | 2.8224 MHz | 6.802… |
| 32 kHz | 2.048 MHz | 9.375 |
| 16 kHz | 1.024 MHz | 18.75 |
| 8 kHz | 512 kHz | 37.5 |

`CM_PCMCTL = 0x5A000000 | SRC | FRAC | ENABLE` with:

- `SRC = 1` → crystal oscillator (19.2 MHz, always available).
- `FRAC = 1<<9` → fractional (MASH) divider; used whenever `DIVF != 0`.
- `ENABLE = 1<<4`.

`CM_PCMDIV = 0x5A000000 | (DIVI << 12) | DIVF`, where `DIVF` is the 12-bit
fractional part (`divisor = DIVI + DIVF/4096`).

For 48 kHz: `DIVI = 6`, `DIVF = 1024` → exactly `6.25`, giving a precise
3.072 MHz BCLK.

> Fractional (MASH) dividers require `DIVI >= 2`; all supported rates satisfy
> this. Writes to the clock manager must be prefixed with the password
> `0x5A000000` or they are ignored.

## PCM registers

### `CS` (0x00)

| Bit | Name | Use |
| --- | --- | --- |
| 25 | STBY | set to leave standby mode |
| 23 | RXSEX | sign-extend received samples |
| 20 | RXD | RX FIFO contains data (polled) |
| 4 | RXCLR | clear RX FIFO (one-shot) |
| 3 | TXCLR | clear TX FIFO (one-shot) |
| 2 | TXON | transmit on |
| 1 | RXON | receive on |
| 0 | EN | module enable |

Initialisation sequence:

1. `CS = 0` (disabled)
2. write `MODE`, `RXC`, `TXC`
3. enable the PCM clock (CM registers)
4. `CS = EN | STBY | RXSEX | RXCLR | TXCLR`
5. wait ≥ 2 PCM clock cycles (a 1 ms `bcm2835_delay`)
6. `CS |= RXON`

### `MODE` (0x08)

Value used: `FLEN(63) | FSLEN(32) | CLKI | FSI`

| Field | Bits | Value | Meaning |
| --- | --- | --- | --- |
| FLEN | 19–10 | 63 | frame length − 1 → 64 bits per frame |
| FSLEN | 9–0 | 32 | frame sync length → 50% duty cycle WS |
| CLKI | 22 | 1 | normal clocking, sample on rising edge |
| FSI | 20 | 1 | frame start on the falling edge (I2S) |
| CLKM | 23 | 0 | **master** (BCLK generated internally) |
| FSM | 21 | 0 | **master** (WS generated internally) |

### `RXC` / `TXC` (0x0C / 0x10)

Each half of the register describes one channel slot:

- channel 1 = bits 31–16, channel 2 = bits 15–0
- `CHxEN` (bit 14 of the field) = channel enabled
- `CHxWEX` (bit 15) + `CHxWID` (bits 0–3) = word width − 8 (32-bit → `WEX=1`,
  `WID=8`)
- `CHxPOS` (bits 4–8) = position of the data within the frame; `1` for I2S
  (MSB one bit after the frame-sync edge)

For stereo 32-bit I2S at the default delay of one bit:

```
RXC = CH1(EN|WEX|WID=8 | POS=1) << 16  |  CH2(EN|WEX|WID=8 | POS=33)
    = 0xC018C018
```

## RX FIFO read

A word becomes available when `CS.RXD` is set; each read of the `FIFO`
register (0x04) pops one 32-bit slot. A stereo frame consumes two words (left,
then right). The driver polls `RXD` in a bounded loop.

## Sample alignment

The INMP441 transmits 24-bit two's-complement samples, MSB first, in 32-bit
slots — the data always occupies the **upper 24 bits** of the slot. Because
the BCM2835 controller stores the slot verbatim, the recovered value is:

```cpp
int32_t sample24 = static_cast<int32_t>(rawSlot) >> 8;  // arithmetic shift
```

`rawSlot >> 16` yields a 16-bit sample directly. Verify the alignment on your
board with the `--dump` mode (see [usage.md](usage.md#sample-alignment)).

## Notes / pitfalls

- If the Linux kernel has an I2S overlay active, it also owns `CM_PCM` and the
  peripheral. Remove any `dtoverlay=...i2s...` lines before using this driver.
- The `BCM2835 ARM Peripherals` datasheet clock offsets are historically
  unreliable; this project follows the kernel driver offsets (`0x98`/`0x9C`).

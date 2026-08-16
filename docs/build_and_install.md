# Build & Install

## Requirements

- Raspberry Pi (BCM2835 family) — this project targets the **Zero 2 W**
- Raspberry Pi OS **32-bit (armhf)** or **64-bit (arm64)** — both are supported
- ~50 MB free disk, internet access for the first install

## 1. Install dependencies (on the Pi)

```bash
cd inmp441_rpi
sudo bash scripts/install_dependencies.sh
```

This installs:

- `build-essential` (g++, make), `git`, `wget`, `curl`
- `nlohmann-json3-dev` — `config.json` persistence
- `libasound2-dev`, `libgpiod-dev`, `pkg-config` — ALSA capture of the I2S
  overlay (`dtoverlay=inmp441-bare`) and the L/R select line (GPIO21); this
  makes recording possible **without root**
- `libmpg123-dev`, `libao-dev` — MP3 playback (`--player`)
- `lame` — MP3 encoding (`--mp3` mode)
- `bluez`, `pulseaudio`, `pulseaudio-module-bluetooth`, `pulseaudio-utils`
  — Bluetooth A2DP playback (`bluetoothctl` + `pactl`)
- the **bcm2835** userspace library v1.71 (`/usr/local/include/bcm2835.h`,
  `/usr/local/lib/libbcm2835.a`) — used **only** by the OLED display
  (SSD1306, menu/player screens), which talks I2C (`i2c-1`), a different
  peripheral from the audio I2S bus

The script detects the architecture automatically and works identically on
32-bit and 64-bit images. On an **x86_64 PC** it additionally installs the ARM
cross toolchain (`g++-arm-linux-gnueabihf`) plus the armhf multiarch audio
libraries (`libmpg123-dev:armhf`, `libao-dev:armhf`, `libasound2-dev:armhf`,
`libgpiod-dev:armhf`), so you can build with `scripts/cross_build.sh` without
the Pi connected.

## 2. Build

On the Pi:

```bash
make clean && make -j4
```

On an x86_64 PC (produces an armhf binary that runs on the Pi, needs the
sysroot populated once from a 32-bit Pi — see the script header):

```bash
bash scripts/cross_build.sh
```

Object files land in `obj/` (mirroring the `src/` tree) and the binary is
written to `bin/inmp441_rpi`.

| Command | Effect |
| --- | --- |
| `make` | build the binary |
| `make -j4` | parallel build |
| `make test` | build & run unit tests (no hardware needed) |
| `make clean` | remove `obj/` and `bin/` |
| `make run` | run the binary (recording needs no root; use `sudo` only for the OLED display) |

## 3. Remote build (SSH workflow)

The project is designed for a clone-and-build flow over SSH. From your
development machine:

```bash
ssh admin@localhost "cd /home/admin && \
  git clone <REPO_URL> && cd inmp441_rpi && \
  git pull && make clean && make -j4 && make run"
```

`<REPO_URL>` is the SSH or HTTPS clone URL of `siliconvalleyar-oss/inmp441_rpi`
(depending on the credentials configured on your machine).

- `make run` runs the binary without `sudo`; recording needs no root. Add
  `sudo` only when you want the OLED display in the menu/player screens.
- Extra CLI options can be forwarded with `ARGS`:
  `make run ARGS="--wav test.wav -d 5"`.

## 4. Run

```bash
./bin/inmp441_rpi --info      # show hardware config
./bin/inmp441_rpi --level     # live level meter
./bin/inmp441_rpi --wav mic.wav -d 10   # record 10 s
```

Recording (`--wav` / `--mp3` / `--level`) needs **no root** (ALSA + libgpiod).
Root is only required by the **OLED display** in the menu/player screens, so
use `sudo ./bin/inmp441_rpi` (menu) or `--player` only if the OLED is wired.

See [usage.md](usage.md) for the full command reference.

## Verifying the build artifacts

```
obj/
├── audio/
│   ├── AlsaDeviceFinder.o
│   ├── AudioProcessor.o
│   └── INMP441.o
└── core/
    ├── Config.o
    ├── Logger.o
    └── SignalHandler.o
bin/
└── inmp441_rpi
```

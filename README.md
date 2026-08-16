# inmp441_rpi

Reads a TDK InvenSense **INMP441** MEMS microphone through **ALSA**, backed by
the kernel I2S driver exposed by the `dtoverlay=inmp441-bare` overlay (the Pi's
I2S peripheral is owned by the kernel). The L/R channel select (GPIO21) is
driven with **libgpiod**, so recording does not need root. **bcm2835** is still
used, but only by the OLED display (menu/player). Written in **C++17**, targets
the **Raspberry Pi Zero 2 W**, and runs on both 32-bit and 64-bit Raspberry Pi
OS.

```
GPIO18 ─── BCLK ──► INMP441 SCK
GPIO19 ─── WS   ──► INMP441 WS
GPIO20 ◄─── SD   ── INMP441 SD (24-bit I2S data)
GPIO21 ─── L/R  ──► channel select (left by default)
```

## Features

- **ALSA capture** through the kernel I2S driver (`dtoverlay=inmp441-bare`):
  the Pi is the I2S master, so no codec or MCLK is needed.
- **Sample-accurate alignment** for the INMP441's 24-bit left-justified output
  (MSB-aligned inside each 32-bit S32_LE slot).
- Three modes:
  - **level** — live RMS/peak meter in dBFS (default)
  - **wav** — record PCM 16-bit mono WAV
  - **dump** — print raw 32-bit slots to verify wiring/alignment
- Run modes are independent of the driver: stdout stays clean for data.
- Pure signal code is **host-testable** (`make test`, no Pi required).
- Slim toolchain: `libasound2-dev` + `libgpiod-dev` (via `pkg-config`) for
  capture, `bcm2835` for the OLED display, plain `Makefile`.

## Repository structure

```
inmp441_rpi/
├── Makefile
├── README.md
├── LICENSE
├── include/            # public headers
│   ├── core/           #   Config, Logger, SignalHandler
│   └── audio/          #   SampleFormat, AlsaDeviceFinder, INMP441, AudioProcessor
├── src/                # implementations (+ main.cpp)
│   ├── core/
│   └── audio/
├── obj/                # build objects (mirrors src/) — gitignored
├── bin/                # binary output — gitignored
├── docs/               # hardware, usage, architecture
├── scripts/            # dependency installer, build/run helpers
└── tests/              # host unit tests
```

## Quick start (on the Raspberry Pi)

```bash
# 1. clone the repository
git clone <REPO_URL> && cd inmp441_rpi

# 2. install dependencies (ALSA + libgpiod + pkg-config; bcm2835 for the OLED)
sudo bash scripts/install_dependencies.sh

# 3. enable the kernel I2S overlay and reboot
#    (add `dtoverlay=inmp441-bare` to /boot/config.txt — see docs/hardware_setup.md)

# 4. build
make clean && make -j4

# 5. verify the hardware and capture audio (no root needed)
./bin/inmp441_rpi --info
./bin/inmp441_rpi --dump 16        # raw slots (wiring/alignment check)
./bin/inmp441_rpi --wav mic.wav -d 10
```

> **Important:** the kernel I2S overlay must be active. Add
> `dtoverlay=inmp441-bare` to `/boot/config.txt` and reboot — the recorder
> reads the mic through that ALSA device.

## Remote build over SSH

```bash
ssh admin@localhost "cd /home/admin && git clone <REPO_URL> \
  && cd inmp441_rpi && git pull && make clean && make -j4 && make run"
```

`make run` needs no `sudo` (recording uses ALSA + libgpiod); forward extra
options with `make run ARGS="--wav test.wav -d 5"`. Use `sudo` only when you
need the OLED screen (menu/player).

## Documentation

| Document | Contents |
| --- | --- |
| [docs/hardware_setup.md](docs/hardware_setup.md) | Wiring, pinout, board notes |
| [docs/architecture.md](docs/architecture.md) | Modules and data flow |
| [docs/build_and_install.md](docs/build_and_install.md) | Build/install/SSH workflow |
| [docs/usage.md](docs/usage.md) | Command reference and examples |
| [docs/testing.md](docs/testing.md) | Unit tests and hardware validation |

## Commit conventions

Commit messages follow **Conventional Commits** (see `docs/LEARNINGS.md`):

```
type(optional scope)!: description
```

| Type | Use |
| --- | --- |
| `feat:` | new feature |
| `fix:` | bug fix |
| `refactor:` | code change that neither fixes a bug nor adds a feature |
| `docs:` | documentation only |
| `test:` | tests only |
| `chore:` | maintenance (build, tooling, dependencies) |
| `build:` / `ci:` / `perf:` / `style:` | dedicated build/CI/performance/style changes |

Every meaningful commit gets a version tag (`vX.Y.Z`); the `VERSION` file at
the repo root always matches the latest tag without the `v` (e.g. tag `v1.7.1`
→ `VERSION` = `1.7.1`). See `docs/LEARNINGS.md` for the full rules.

A `commit-msg` git hook validates the format automatically — install it with:

```bash
bash scripts/install_commit_hook.sh
```

## License

MIT — see [LICENSE](LICENSE).

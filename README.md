# inmp441_rpi

Reads a TDK InvenSense **INMP441** MEMS microphone over the Raspberry Pi's
**PCM/I2S** peripheral — directly from userspace via the **bcm2835** library,
no ALSA, no kernel modules. Written in **C++17**, targets the **Raspberry Pi
Zero 2 W**, and runs on both 32-bit and 64-bit Raspberry Pi OS.

```
GPIO18 ─── BCLK ──► INMP441 SCK
GPIO19 ─── WS   ──► INMP441 WS
GPIO20 ◄─── SD   ── INMP441 SD (24-bit I2S data)
GPIO21 ─── L/R  ──► channel select (left by default)
```

## Features

- Direct register access to the BCM2835 **PCM/I2S controller** and its clock
  (`CM_PCM`) — the Pi is the I2S master, so no codec or MCLK is needed.
- **Sample-accurate alignment** for the INMP441's 24-bit left-justified output.
- Three modes:
  - **level** — live RMS/peak meter in dBFS (default)
  - **wav** — record PCM 16-bit mono WAV
  - **dump** — print raw 32-bit slots to verify wiring/alignment
- Run modes are independent of the driver: stdout stays clean for data.
- Pure signal code is **host-testable** (`make test`, no Pi required).
- Minimal toolchain: only `bcm2835` + standard library, plain `Makefile`.

## Repository structure

```
inmp441_rpi/
├── Makefile
├── README.md
├── LICENSE
├── include/            # public headers
│   ├── core/           #   Config, Logger, SignalHandler
│   └── audio/          #   SampleFormat, I2SController, INMP441, AudioProcessor
├── src/                # implementations (+ main.cpp)
│   ├── core/
│   └── audio/
├── obj/                # build objects (mirrors src/) — gitignored
├── bin/                # binary output — gitignored
├── docs/               # hardware, registers, usage, architecture
├── scripts/            # dependency installer, build/run helpers
└── tests/              # host unit tests
```

## Quick start (on the Raspberry Pi)

```bash
# 1. clone the repository
git clone <REPO_URL> && cd inmp441_rpi

# 2. install dependencies (build tools + bcm2835 v1.71)
sudo bash scripts/install_dependencies.sh

# 3. build
make clean && make -j4

# 4. verify the hardware and capture audio
sudo ./bin/inmp441_rpi --info
sudo ./bin/inmp441_rpi --dump 16        # raw slots (wiring/alignment check)
sudo ./bin/inmp441_rpi --wav mic.wav -d 10
```

> **Important:** no I2S kernel overlay may be active (remove any
> `dtoverlay=...i2s...` from `/boot/config.txt`); the driver owns the PCM
> peripheral and its clock exclusively.

## Remote build over SSH

```bash
ssh admin@localhost "cd /home/admin && git clone <REPO_URL> \
  && cd inmp441_rpi && git pull && make clean && make -j4 && make run"
```

`make run` runs with `sudo`; forward extra options with
`make run ARGS="--wav test.wav -d 5"`.

## Documentation

| Document | Contents |
| --- | --- |
| [docs/hardware_setup.md](docs/hardware_setup.md) | Wiring, pinout, board notes |
| [docs/architecture.md](docs/architecture.md) | Modules and data flow |
| [docs/i2s_registers.md](docs/i2s_registers.md) | Low-level register configuration |
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

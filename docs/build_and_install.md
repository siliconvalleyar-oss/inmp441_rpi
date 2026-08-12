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

- `build-essential` (g++, make), `git`, `wget`
- the **bcm2835** userspace library v1.71 (`/usr/local/include/bcm2835.h`,
  `/usr/local/lib/libbcm2835.a`)

The script detects the architecture automatically and works identically on
32-bit and 64-bit images.

## 2. Build

```bash
make clean && make -j4
```

Object files land in `obj/` (mirroring the `src/` tree) and the binary is
written to `bin/inmp441_rpi`.

| Command | Effect |
| --- | --- |
| `make` | build the binary |
| `make -j4` | parallel build |
| `make test` | build & run unit tests (no hardware needed) |
| `make clean` | remove `obj/` and `bin/` |
| `make run` | run with `sudo` |

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

- `make run` invokes the binary with `sudo` automatically.
- Extra CLI options can be forwarded with `ARGS`:
  `make run ARGS="--wav test.wav -d 5"`.

## 4. Run

```bash
sudo ./bin/inmp441_rpi --info      # show hardware config
sudo ./bin/inmp441_rpi --level     # live level meter
sudo ./bin/inmp441_rpi --wav mic.wav -d 10   # record 10 s
```

See [usage.md](usage.md) for the full command reference.

## Verifying the build artifacts

```
obj/
├── audio/
│   ├── AudioProcessor.o
│   ├── I2SController.o
│   └── INMP441.o
└── core/
    ├── Config.o
    ├── Logger.o
    └── SignalHandler.o
bin/
└── inmp441_rpi
```

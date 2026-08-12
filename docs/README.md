# Documentation

Welcome to the **inmp441_rpi** documentation. This project reads audio from a
TDK InvenSense **INMP441** MEMS microphone using the BCM2835 **PCM/I2S**
peripheral directly from userspace through the `bcm2835` library.

## Index

| Document | Contents |
| --- | --- |
| [hardware_setup.md](hardware_setup.md) | Wiring, pinout, board notes and channel selection |
| [architecture.md](architecture.md) | Directory layout, modules and data flow |
| [i2s_registers.md](i2s_registers.md) | Low-level PCM/I2S and clock register configuration |
| [build_and_install.md](build_and_install.md) | Dependencies, building and remote build workflow |
| [usage.md](usage.md) | Command-line usage, modes and examples |
| [testing.md](testing.md) | Unit tests and on-hardware validation tips |

## Quick links

- [README.md](../README.md) — project overview and quick start
- [scripts/](../scripts) — dependency installer, build/run helpers

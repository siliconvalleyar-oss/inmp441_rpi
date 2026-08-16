# Documentation

Welcome to the **inmp441_rpi** documentation. This project reads audio from a
TDK InvenSense **INMP441** MEMS microphone through **ALSA**, backed by the
kernel I2S driver exposed by `dtoverlay=inmp441-bare`; the L/R channel select
(GPIO21) is driven with **libgpiod**, and `bcm2835` is used only by the OLED
display.

## Index

| Document | Contents |
| --- | --- |
| [hardware_setup.md](hardware_setup.md) | Wiring, pinout, board notes and channel selection |
| [architecture.md](architecture.md) | Directory layout, modules and data flow |
| [build_and_install.md](build_and_install.md) | Dependencies, building and remote build workflow |
| [usage.md](usage.md) | Command-line usage, modes and examples |
| [testing.md](testing.md) | Unit tests and on-hardware validation tips |

## Quick links

- [README.md](../README.md) — project overview and quick start
- [scripts/](../scripts) — dependency installer, build/run helpers

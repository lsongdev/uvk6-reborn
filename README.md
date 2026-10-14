# UVK6 Reborn

Open-source firmware for the Quansheng UV-K6, evolved from [Hawk5](https://github.com/fagci/hawk5).

Tune by band, frequency, and explicit demodulation mode. Features include multi-slot monitoring, channel storage, broadcast text, scanning, and frequency capture. This project is under active development. Read the user manual and back up your radio before use.

## Documentation

- [User Manual](docs/user-manual.md): installation, controls, menus, and everyday use.
- [Design](docs/design.md): feature behavior, data model, and roadmap.
- [Development](docs/development.md): build, flashing, source code, tools, and checks.
- [Hardware Facts](docs/hardware.md): device, chips, controls, and storage references.

## Quick Start

With the build environment installed:

```sh
make
make flash PORT=/dev/cu.usbserial-1420
```

Before flashing, turn the radio off, then power it on while holding PTT. Replace the serial port with the one for your device. See the [Development Guide](docs/development.md) for prerequisites, firmware image selection, and recovery steps.

# faderOS

![faderOS](branding/logo.svg)

Open-source control firmware and controller for professional video panels.
Sony BKDS-2010 is the first supported panel. The Linux controller connects the
panel to OBS, vMix, ATEM or [kavtor](https://github.com/kavtor/kavtor), with three
independent server profiles and a built-in configuration website.

The replacement m68k firmware runs on the original panel processor. Original
Sony EPROM recovery is retained. Panel firmware and controller versions are
independent. The first embedded target is RP2350 + W5500; its UART bring-up
probe is available, while embedded network/mixer runtime remains in development.

## Build and run on Linux

Requires CMake 3.20+, a C++17 compiler, Python 3.10+, pkg-config, libcurl 7.86+
with WebSocket support, OpenSSL, libxml2 and nlohmann/json. Test peers also need
Python `websockets`; Node.js enables the configuration-web tests.

```sh
python3 -m venv .venv
. .venv/bin/activate
pip install -r controller/test/requirements.txt
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
build/controller/faderOS --help
build/controller/faderOS --port /dev/ttyUSB0 --baud 38400 --kavtor --web 8088
```

The configuration site is at `http://127.0.0.1:8088`. Keep runtime configuration
and credentials out of Git. The `--strata` CLI spelling remains a compatibility
alias for the renamed kavtor adapter; saved server IDs are unchanged.

## Panel firmware

```sh
make -C firmware protocol
make -C firmware test
```

Building firmware requires a suitable `m68k-elf-` cross toolchain. Host-only
checks do not require a panel or Sony ROMs. Read the [installation guide](firmware/INSTALL.md)
and [RAM updater guide](firmware/SELF_UPDATE.md) before any physical write.
No Sony ROMs, flash dumps, manuals or private hardware backups are distributed.

## Documentation

- [Controller user guide](controller/docs/user-guide.md)
- [Development and architecture](controller/docs/development.md)
- [Panel protocol](controller/docs/panel-protocol.md)
- [Pico bring-up](controller/docs/pico.md)
- [OBS](controller/OBS.md), [vMix](controller/VMIX.md), [ATEM](controller/ATEM.md)
- [Firmware](firmware/README.md), [LCD graphics](firmware/LCD_GRAPHICS.md)
- [Roadmap](controller/docs/roadmap.md), [contributing](CONTRIBUTING.md)
- [Brand assets and printable labels](branding/README.md)

This is an early development project. Local peer tests and physical validation
are documented separately; simulation is not proof of hardware behavior.
Licensed under [GNU GPLv3](LICENSE).

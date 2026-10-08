# faderOS

A control platform for hardware video panels, with OBS, vMix, ATEM and kavtor
mixer adapters. Sony BKDS-2010 is the first supported panel; other panel
integrations are planned.

The current executable runs on Linux. It communicates with the replacement
m68k panel firmware over serial and provides an embedded configuration website.
The first embedded target is RP2350 with W5500 Ethernet and an additional USB
host port. A Pico SDK UART diagnostic application is available; embedded mixer
control and Ethernet are not implemented yet.

## Build

Requires a C++17 compiler, CMake 3.20+, Python 3.10+, pkg-config, libcurl with
WebSocket support, OpenSSL, libxml2 and nlohmann/json. Doxygen is optional.
On Debian/Ubuntu:

```sh
sudo apt install build-essential cmake pkg-config python3 libcurl4-openssl-dev libssl-dev libxml2-dev nlohmann-json3-dev
cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/cmake --parallel
ctest --test-dir build/cmake --output-on-failure
```

CMake checks development dependencies during configuration. Tests use local
protocol peers, not physical mixers. A test build also needs Python `websockets`
(`python3 -m pip install -r test/requirements.txt` in a virtual environment).
See [development](docs/development.md) for installation and API documentation.

## Run

```sh
build/cmake/faderOS --help
build/cmake/faderOS --port /dev/ttyUSB0 --baud 38400 --atem --web 8088
```

Open `http://127.0.0.1:8088` to configure endpoints and mappings. The website is
embedded in the executable and does not require the source working directory.
Configuration files are created in the working directory unless explicit paths
are supplied. Keep credentials and local settings out of version control.

Three independent server profiles can stay connected simultaneously. USER MENU
1–3 selects the controlled mixer. Settings can also be edited from the panel LCD.
Use `--verbose` for button/encoder events, acknowledgements and analog telemetry;
normal output reports connection changes and errors. Console `analog watch`
still explicitly enables per-sample diagnostics.

## Documentation

- [User guide](docs/user-guide.md) and [panel protocol](docs/panel-protocol.md)
- [Architecture and development](docs/development.md)
- [Pico hardware bring-up](docs/pico.md)
- [OBS](OBS.md), [vMix](VMIX.md), [ATEM](ATEM.md)
- [Roadmap](ROADMAP.md) and [changelog](CHANGELOG.md)
- [Contribution workflow](CONTRIBUTING.md)
- [Publication checklist](docs/publication.md)

## Repository layout

`src/link/` is the current runtime and portable control model; `web/` contains
canonical browser assets; `test/link/` contains regression tests and simulated
protocol peers; `tools/` contains asset/catalog generation and publication tools;
`metadata/` contains the physical control catalog; `examples/` contains sanitized configuration examples; `docs/` contains guides.

The other `src/` modules and `test/test_*.cpp` are the earlier injection host.
They are retained as reference and can be built with `make legacy`; they are not
part of the default CMake build. `prototype/` is the Python reference protocol
client. It is not RP2040 firmware.

## Publication status

Not published yet. The project name is **faderOS**, with this exact capitalization.
The license must be selected before release. No license grant is implied by the presence of these sources.
Sony ROMs, dumps, manuals and personal runtime configuration are excluded from
prepared publication archives. This project is not affiliated with Sony,
Blackmagic Design, OBS or vMix.

## Naming and compatibility

The current workspace directory remains `picohost/`. New builds provide the
`faderOS` executable; `picohost-link` remains a compatibility entry point for
existing launchers. Internal CMake targets/options, configuration filenames and
wire protocol identifiers retain their existing names. The Sony panel firmware
is the BKDS-2010 implementation of faderOS; its version is independent of the
host version. kavtor remains a separate mixer project with its own pending name.

Panel hardware diagnostics are available in SYSTEM SETUP > TEST; see [panel tests](docs/panel-test.md).

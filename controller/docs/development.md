# Development

## Build and dependencies

CMake is the supported standalone build. `make link` and `make test-link` remain
compatible with the BKDS workspace and its emulator launcher. `make` now builds
the current host; `make legacy` builds the earlier injection runtime.

CMake creates `picohost_core` (control model without exceptions/RTTI),
`picohost_adapters` (Linux network/web adapters) and `faderOS` (serial
transport, scheduling and CLI). Adapter implementations may use exceptions.
Runtime dependencies are detected with imported CMake/pkg-config targets.
libcurl must include WebSocket support for OBS; distribution builds vary.

```sh
cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build/cmake --parallel
ctest --test-dir build/cmake --output-on-failure
cmake --install build/cmake --prefix /tmp/controller-install
```

Use `-DBUILD_TESTING=OFF` for an installation without test executables.
Tests retain assertions in Release builds. CTest passes the actual build path
to Python peers via `PICOHOST_TEST_BUILD_DIR`. Tests need localhost sockets and
PTY access but do not contact mixers or open the physical panel port.

## Web assets

Edit `web/index.html`, `web/app.js` and `web/styles.css`. CMake generates
`generated/web_page.hpp` in its build directory. The Make build updates the
compatibility header with `python3 tools/embed_web.py`. Never edit generated
headers manually. JavaScript syntax can be checked with `node --check web/app.js`.
Node is optional for the runtime build.

## Documentation and language

English is the primary language for code comments, CLI, LCD, web and docs.
Protocol identifiers and user-supplied mixer names must not be translated.
With Doxygen installed, `cmake --build build/cmake --target docs` creates HTML
API documentation. The portable interfaces live in `src/link/core.hpp`,
`mixer.hpp`, `panel.hpp`, `config.hpp` and adapter headers.

## Validation boundaries

Protocol peer tests verify control and synchronization logic. They cannot prove
serial electrical behavior or panel MMIO. For firmware changes, Ghidra assembly
and the physical panel are the sources of truth; MAME is a partial model.
Physical acceptance remains necessary for changes to timing or hardware behavior.

See the [API overview](api.md) and [dependency inventory](third-party.md).

## Embedded preparation

The agreed first hardware target is RP2350 with W5500 Ethernet, native USB
for device maintenance, and an additional USB host path. ESP32/Wi-Fi/BLE are
outside the first-version scope. The exact board, USB host implementation and
pin assignment are still to be selected.

Build the shared control model without Linux adapters or their dependencies:

```sh
cmake -S . -B build/core -DPICOHOST_BUILD_POSIX=OFF
cmake --build build/core --parallel
ctest --test-dir build/core --output-on-failure
```

Explicit `-DFADEROS_PLATFORM=core` selects the same standalone build.
See [Pico bring-up](pico.md) for platform macros and the SDK build.

This builds the existing core regression suite with simulated transports and
mixers. Cross-compilation skips this native executable. This core-only build does not provide a runnable controller. The separate
`pico` target provides a UART probe; embedded Ethernet/mixer control is pending.
A successful native core build is not a RAM/flash fit or hardware validation.
The model still uses C++ strings, vectors and stream-based configuration; heap
usage must be measured and persistence adapted before deployment.

Port in stages: UART/session at 9600 then 38400; one ATEM connection and basic
buses; persistent settings and web; remaining adapters and three simultaneous
profiles; finally USB host/Stream Deck. Keep each stage physically testable.
Reserve an explicit socket budget for DNS, DHCP, NTP, web clients and mixer
sessions rather than assuming that eight W5500 sockets guarantee all workloads.
USB host power must be current-limited and isolated from USB device VBUS.

# Pico hardware bring-up

The first embedded target is RP2350 with W5500 and an additional USB host port.
Linux remains the default platform and the complete mixer controller. The Pico
application currently validates only the panel serial link. It has no Ethernet,
web server, settings persistence, mixer adapter, self-update client or USB host.
The original RP2040 Pico can also be selected for an initial UART test.

## Build selection

`FADEROS_PLATFORM` selects `posix` (Linux), `pico` (Pico SDK) or `core` (library
and native core tests). Platform macros are propagated to the shared code:
`FADEROS_PLATFORM_POSIX`, `FADEROS_PLATFORM_PICO`, `FADEROS_PLATFORM_CORE`.
`src/link/platform.hpp` rejects conflicting selections. Direct Linux Make builds
retain the POSIX default. Add conditional code only at hardware boundaries;
framing, gestures and control logic remain shared.

Use separate build directories; CMake cannot switch between native and embedded
toolchains in one existing directory. No automatic SDK download is performed.
Install Pico SDK 2.0 or newer and the ARM embedded C/C++ toolchain first.

```sh
# Full Linux runtime, unchanged adapters and configuration files.
cmake -S . -B build/linux -DFADEROS_PLATFORM=posix
cmake --build build/linux --parallel
ctest --test-dir build/linux --output-on-failure

# UART probe for a standard Raspberry Pi Pico 2.
cmake -S . -B build/pico2 -DFADEROS_PLATFORM=pico \
  -DPICO_SDK_PATH=/path/to/pico-sdk -DPICO_BOARD=pico2 \
  -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build/pico2 --parallel
```

The embedded build produces `faderOS-pico.uf2`, ELF and binary files. For a
standard original Pico, use another build directory and `-DPICO_BOARD=pico`.
Select the actual SDK board definition for integrated Ethernet boards before
flashing; flash size and GPIO assignments can differ. This UF2 is for the Pico,
not for the Sony panel flash.

## UART wiring

Defaults are UART0, TX GPIO0, RX GPIO1, startup 9600 and preferred 38400 baud.
Override with `FADEROS_UART_INDEX`, `FADEROS_UART_TX`, `FADEROS_UART_RX` and
`FADEROS_PANEL_BAUD` CMake cache options. TX/RX must be valid pins for the chosen
UART and must not be reserved by the board's Ethernet/flash circuitry.

- Pico TX -> MAX3232 TTL input -> Sony RS232 receive.
- Sony RS232 transmit -> MAX3232 RS232 input -> Pico RX.
- Common signal ground. Supply the MAX3232 logic side at 3.3 V with its required
  charge-pump capacitors; verify the actual module does not drive RX at 5 V.
- Never connect RS232 voltage levels directly to Pico GPIO.

Use the native USB connector for power and USB CDC diagnostics during bench
bring-up. Do not join PC USB VBUS to the Sony 5 V rail without a reviewed power
path. Ethernet and the additional USB host need not be connected for this test.

## First physical test

Close Linux faderOS and other users of the panel serial port. Boot the Pico with
the probe UF2 and the Sony with the already validated binary-link firmware.
The probe starts the serial session immediately; USB enumeration is not required
for the link to run. Open its USB CDC console to see a periodic version/status
line, firmware identification, button DOWN/UP, raw encoder deltas and analog
values/rates. USB logging is best-effort and must not block the panel link.

The shared Session performs negotiation and falls back according to the existing
panel protocol. The transport uses an interrupt-driven 1024-byte RX ring and
nonblocking FIFO writes. An RX ring overflow closes the probe session and asks
for a Pico reset rather than silently reporting success.

Once ready, the probe clears/synchronizes panel outputs using the existing Panel
scheduler and displays `faderOS UART BRING-UP` / `NO MIXER / USB DIAGNOSTICS`.
It does not perform mixer actions or light pressed keys as a demonstration.
Verify 9600 startup, 38400 readiness, correct button IDs, analog motion and
reconnection after restarting the panel. Repeat at preferred 9600 if necessary.
Compilation and simulated tests do not validate physical wiring or timing.

## LCD graphics test

With panel firmware 0.23, type `g` in the Pico USB CDC console to load and show
all eight arrow glyphs, `b` to display a 3x2-cell / 15x16-pixel arrow, and `t` to
return to ASCII diagnostics. The probe checks capability bit 9 before trying
these commands. No mixer action is involved. Glyph writes and cell refreshes use
the same shared scheduler as Linux; physical validation is still pending.

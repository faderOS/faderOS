# faderOS panel firmware — Sony BKDS-2010

Replacement firmware for the Sony TMP68301 / Motorola 68000 panel. The Sony
EPROM is retained for independent recovery. Current panel application version
is 0.24; the resident loader has its own revision. Stable protocol identifiers
remain unchanged by project branding.

Validated on the physical panel: startup, button scanning, low/high LED states,
LCD/7-segment service, encoder events, analogue acquisition, 38400-baud operation,
RAM self-update with readback verification and remote boot. LCD CGRAM graphics
are supported. MAME and earlier injection models are not hardware authority.

## Layout

- `boot/`: resident validation/entry
- `app/`: standalone driver demo and serial-link application
- `drivers/`: UART, KY scan/lamps, LCD, segments, analogue and timing
- `common/`: image checks, protocol and debounce
- `update/`: RAM-only flash updater
- `platform/`, `include/`, `ld/`: CPU startup, board interface and link layout
- `tools/`: packing, Sony monitor installation and update clients
- `tests/`: host-side checks and protocol/update fixtures

```sh
make -C firmware protocol
make -C firmware test
make -C firmware test-self-update
```

`protocol` creates `build/flash-link.bin`, a combined 1 MiB physical flash image.
It is not an Intel HEX transport file. The Sony installer generates its required
transport. `m68k-elf-gcc` and `m68k-elf-objcopy` are the default cross tools;
`CROSS` can select a validated equivalent. Never assume a different ABI/libgcc
configuration works on a 68000 merely because compilation succeeds.

Flash layout: resident sector 0, application sectors 1–6, sector 7 reserved.
The EPROM and sector 7 are excluded from update writes. Keep a verified physical
backup and recovery path. See [INSTALL](INSTALL.md), [SELF_UPDATE](SELF_UPDATE.md),
[serial](BAUD_PHYSICAL.md), [analogue stream](ANALOG_STREAM.md) and [KY notes](KY_BRINGUP.md).

GNU GPLv3. Original Sony software and reference material are not included.

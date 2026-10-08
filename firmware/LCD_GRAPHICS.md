# LCD custom graphics

Panel firmware 0.24 and faderOS 0.9.3 add custom glyph uploads and binary LCD
cells. Physical validation is pending; compilation and host tests are not proof
of panel behavior.

## Original Sony implementation

Ghidra assembly confirms the original firmware already supports CGRAM:

- `FUN_00047eb8` writes commands/data through control register `0x300003` and
  data register `0x300007`, with RS and enable handling.
- `FUN_0004805e` selects `0x40 | (slot << 3)` and writes eight row bytes using
  that same byte-write routine.
- `FUN_00047f44` initializes a two-line, 5x8 display with command `0x38`, clears
  it, enables increment mode and loads two custom patterns through `0x4805e`.

The new driver uses the existing validated LCD bus routine, without changes to
KY scanning, panel gating or baud negotiation.

## Limits and scheduling

There are eight simultaneous 5x8 patterns: 320 independently programmable bits.
Patterns may be reused across the display. Redefining a slot changes every cell
that uses it; callers must coordinate slot ownership. Character-cell gaps remain
visible, so this is not a continuous pixel framebuffer.

`LCD_GLYPH` (`0x16`) accepts a first slot followed by one to eight contiguous
patterns, eight row masks each. Each mask must be 0..31. The entire batch is
validated before modifying desired state. The driver writes at most four CGRAM
rows per service call and restores DDRAM addressing afterwards.

`LCD_CELLS` (`0x17`) accepts an offset 0..79 and one or more cells within the
80-cell display. Glyph codes 0..7 and ROM codes 32..255 are allowed; 8..31 are
rejected. Existing ASCII commands remain unchanged. ACK means accepted into the
desired state, not that the physical LCD has finished updating. Pending glyphs
are uploaded before cell refreshes.

HELLO capability bit 9 advertises both commands. The shared host checks it and
uses ASCII `?` fallbacks with older firmware. `LcdBitmap` supports small tiled
drawings, including a 3x2-cell / 15x16-pixel icon.

## Identity and physical test

Before a host connects, the panel shows `faderOS BKDS-2010 FW 0.24` and
`WAITING FOR HOST`. Panel and host versions are independent; binary protocol
schema 1 and the RAM updater handshake are unchanged.

After installing `build/flash-link.bin` with the existing self-update procedure,
close other serial clients and run from the workspace root:

```sh
python3 firmware/tools/lcd_graphics_demo.py --port /dev/ttyUSB0 --mode arrows
python3 firmware/tools/lcd_graphics_demo.py --port /dev/ttyUSB0 --mode bitmap
```

Each command opens a diagnostic session at 9600; it does not flash or control a
mixer. Exit with Ctrl-C and allow the panel watchdog to restore its waiting
screen before reconnecting the normal host. Check all eight arrows, the tiled
icon, text restoration, panel stability and normal controls afterwards.

The Pico UART probe offers the same checks through USB CDC: `g` for eight arrows,
`b` for the tiled icon and `t` to restore its ASCII diagnostic screen.

## Flash icon library

Firmware 0.24 stores eight direction icons in `include/lcd_icons.h` (64 bytes).
Command `LCD_ICONS` (0x18), advertised by capability bit 10, selects a starting
CGRAM slot and 1–8 library IDs. IDs 1–8 are up, down, left, right, top-left,
top-right, bottom-left and bottom-right. The host recognizes these patterns and
sends IDs instead of row data; arbitrary glyph uploads still work in every slot.
Library selection still requires CGRAM writes, but saves serial transfer.

The application budget is 786400 bytes across sectors 1–6. Before this library,
the application image occupied 19188 bytes. Even 10000 5x8 patterns (80000 bytes,
excluding indexing/code) would fit with that image inside one 128 KiB sector.
Do not confuse flash library capacity with the eight simultaneous LCD patterns.

### Planned Sony wipe catalogue

The user is preparing 400 pictograms at 4x2 character cells (20x16 pixels).
Each image consumes all eight CGRAM slots and 64 flash bytes, so the bitmap
payload is 25600 bytes (25 KiB), plus an index keyed by Sony wipe number.
Normal ROM text can coexist, but custom arrow glyphs cannot remain loaded at
other positions while all slots contain the pictogram. Larger catalogue access
will need a separate 16-bit picture selector; the current LCD_ICONS command
only defines eight stable direction IDs. Do not truncate Sony IDs to one byte.

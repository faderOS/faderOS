# API overview

`core.hpp` defines framing, transport and session listeners for the m68k binary
link. `panel.hpp` schedules and synchronizes panel outputs. `mixer.hpp` defines
shared mixer state, capabilities and the adapter contract. `config.hpp` owns
profile persistence and revision conflict checks. `transitions.hpp` and
`keys.hpp` implement panel gestures and editing state.

Network workers own adapter protocol state behind their mutexes. The host loop
samples published state and dispatches explicit user actions. Inactive profiles
receive state without replaying controls. Web edits are applied through the host
loop; revision and server-slot checks reject stale edits.

Generate the browsable API with the CMake `docs` target when Doxygen is present.
The POSIX implementation is not the RP2350 hardware transport. New embedded
transports should preserve the portable core interfaces and concurrency contract.

`Panel::lcd_glyph(slot, rows)` defines a 5x8 pattern; `Panel::lcd_cells(offset,
data, count)` writes raw cell codes without C-string truncation. `lcd_graphics.hpp`
provides arrow patterns and `LcdBitmap`, a fixed-size tile composer using at most
eight slots. `LcdBitmap::draw` validates bounds before updating the display model.
ASCII `Panel::lcd` remains the normal text API. See the panel protocol for slot
ownership, compatibility fallback and acknowledgement semantics.

Matching direction glyphs automatically use panel ROM IDs when available; custom glyph APIs and slot ownership remain unchanged.

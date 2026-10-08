# KY scan and lamp bring-up

Physical validation resolved the early floating-bus scans and random lamps.
Buttons, low/high lamp planes and panel setup are validated from panel 0.14.
Read-before-write/select timing and initialization were checked against Sony
assembly and physical behavior; decompiled MMIO omissions and emulator models
were not used as final authority.

Keep panel gate **0x08**. The unrelated D00001 bit 1 caused resets during early
bring-up; do not add it to initialization. KY scan/lamp sequences belong in
`drivers/ky.c`, with debounce/event timing above the driver layer.

For future changes, verify a dark/safe boot, one correct DOWN/UP pair per button,
multibutton operation, both lamp planes and no input flood when a host connects.
Check the physical panel after every firmware change. Host tests and MAME may
reject hypotheses but cannot establish undocumented electrical timing.

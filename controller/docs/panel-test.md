# Panel diagnostics

Open SYSTEM SETUP (button 47), then F5 TEST. EXIT always returns from a test
page to the test menu; EXIT in the test menu returns to SYSTEM SETUP. Pressing
SYSTEM SETUP during a test also returns to the setup root.

The first menu page offers F1 KEYS, F2 LEDS, F3 ENC, F4 ANLG, F5 LCD and F6 MORE.
The second page offers F1 7SEG, F2 DEMO, F3 BEEP and F4 INDIC. Use MORE or the
LCD up/down buttons to switch pages.

- KEYS shows the last edge, simultaneous held count and key IDs. Held keys
  light HIGH; EXIT and SYSTEM SETUP retain their navigation roles.
- LEDS: F1 OFF, F2 LOW, F3 HIGH, F4 CHASE, F5 alternating LOW/HIGH, F6 BEEP.
  Mono-state lamps light identically in LOW/HIGH. EXIT remains lit for navigation.
- ENC shows raw deltas before normal menu sensitivity scaling, and all six
  cumulative counters. Each F key zeros its corresponding counter.
- ANLG shows raw T-bar (12 bits) and joystick X/Y (8 bits each), without changing
  calibration. LCD updates are limited to 10 Hz; serial sampling remains active.
- LCD: F1 ASCII text, F2 all pixels on, F3 all eight custom arrows (firmware
  graphics support required). LCD drawings remain transferable over serial.
- 7SEG: F1 all segments including decimal points and side legends, F2 cycling
  digits, F3 walking digit/side legends, F4 blank.
- INDIC: F1 walking indicators, F2 all on, F3 off.
- BEEP emits the normal bounded short pulse; it never latches the buzzer on.
- DEMO scrolls TEST/DEMO as a 12x4 bus pixel banner, alternating LOW/HIGH.

Diagnostics capture all panel button edges and raw encoder/analog values before
mixer routing. Background mixer transports remain connected. On exit, saved
panel outputs are restored, normal feedback resumes from current mixer state,
and manual transition pickup is reset so movement during testing is not applied
to a mixer afterwards. This is a host feature; no new Sony flash is required.
Physical verification of each diagnostic page is still required.

## Sony wipe image input

Preferred input is one monochrome PNG per Sony number, named `sony-023.png`,
exactly 20x16 pixels, white for on and black for off, without anti-aliasing or
resampling. Store the normal orientation, not a reversed transition. One image
converts into eight 5x8 character blocks (4 columns, 2 rows), 64 bytes in the
current row-mask representation. A text/CSV index with Sony number and optional
English label can accompany the files. No baked-in character-cell gaps are
needed; those are physical properties of the LCD. Keep larger original artwork
separately if available; only native pixels are used for the panel.

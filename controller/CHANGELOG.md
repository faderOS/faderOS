# Changelog

## 0.20.0

- Add kavtor SuperSource preview/program delegation on the two empty delegation
  buttons, KEY window selection and AUX source hot punches.
- Preserve the selected KEY position across takes without automatically selecting
  program delegation. Include bus/M/E/instance/window context in requests.
- Parse bounded active-bus metadata and refresh it on tally changes; window errors
  do not cancel the main transition.


## 0.19.0

- Complete the kavtor web workspace with both 12-position banks, all four keyers and four NEXT TRANSITION key selections.
- Add capability-aware Transition Preview with confirmed state and disconnected/busy feedback.
- Enable WIPE border colour selection and correct border state display.

## 0.18.0

- Discover native kavtor DIP and use it for MIX keypad 2, with VFADE fallback for older servers.

- Select MIX/VFADE/FADECUT/CUTFADE from keypad 1–4, including manual operation.
- Select timed PUSH/SLIDE entry directions from DME keypad 1–8, with REV support.
- Use the explicit Sony namespace for the ten confirmed panel wipe pictograms; preserve explicit SMPTE API requests.
- Disable unsupported DIRECT codes using the server's advertised catalogue.

## 0.17.0

- Control kavtor LINEAR/CHR key types and MAIN MASK through the existing upstream/DSK delegation.
- Read confirmed independent processing state and advertise controls only when the server supports them.
- Expose processing parameters in kavtor web status and reject unsupported luma/separate key operations.


## 0.16.0

- Drive kavtor MIX and background WIPE from the calibrated T-bar with coalesced position updates.
- Freeze manual takes across M/E delegation and add lever pickup, endpoint cancel/complete and blinking direction indicators.
- Keep AUTO TRANS off during manual takes and verify the TCP adapter and portable pickup logic.


## 0.15.0

- Follow SHIFT with kavtor multiview bank paging and consume its 24-source availability.
- Add configuration delegation for KEY3/KEY4 and DSK2 by double-click, with blinking feedback.
- Route kavtor AUX1–AUX4 from the AUX bus while keeping main buses isolated.
- Add kavtor safe-area framing and EBU/classic margins to LCD and web configuration.
- Cover upper-bank key feedback, delegation and AUX operation with regression tests.


## 0.14.1

- Discover kavtor TRANS PREVIEW support from panel state; double-click a
  Transition Type button to rehearse and single-click to return to normal.
- Keep preview source tally off air during a rehearsal, and reset discovered
  support on disconnect for compatibility with older kavtor versions.
- Validate rehearsal mode changes with the kavtor protocol peer tests.


## 0.12.1

- Move vMix Merge to DME keypad 0. Compact MIX to 1 Fade, 2 AlphaFade and
  3 CrossZoom, migrating saved mappings without losing other assignments.

## 0.12.0

- Add vMix keypad families: four MIX types, seven DME effects with separate
  reverse bindings, four default Sony wipe codes and eight native stingers.
- Keep keypad preparation separate from the armed Transition Type.
- Migrate legacy transition mappings and expose all family slots in the web.
  Stinger total length is local completion tracking metadata; native animation
  timing remains configured in vMix.

## 0.11.0

- Add vMix MIX and DME effect mappings, Sony WIPE NORM/REV bindings and a
  Transitions web tab. Unassigned effects are rejected with a short panel beep.
- Add native vMix FTB with state readback, including external changes. FTB uses
  the duration configured in vMix; its API exposes no duration argument.
- Preserve version-1 input mapping files and reject invalid effect names before
  replacing a profile.

## 0.10.0

- Add SYSTEM SETUP > TEST: multibutton feedback, raw encoder deltas/counters,
  independent analog readings, LED patterns, LCD glyphs, 7SEG, indicators,
  buzzer and scrolling TEST/DEMO bus pixel banners.
- Route all panel controls to diagnostics while testing; keep mixer connections
  live and re-arm the T-bar on exit. No panel firmware update is required.


## 0.9.3

- Display large 15x16-pixel direction arrows in the ATEM DVE keypad menu.

- Use the panel firmware 0.24 ROM direction-icon library when advertised, with
  custom upload fallback on 0.23. Physical validation remains pending.

## 0.9.2 — embedded bring-up and LCD graphics

- Select Linux, Pico SDK or standalone core builds with `FADEROS_PLATFORM`.
- Add explicit platform macros while preserving the default Linux/Make runtime.
- Add a Pico UART probe with buffered interrupt RX, nonblocking TX, shared panel
  negotiation, configurable pins and best-effort USB CDC diagnostics.
- Ethernet, mixer adapters and USB host remain pending on Pico.
- Define eight 5x8 LCD glyphs, write binary cells, compose small tiled bitmaps and
  fall back to ASCII on panel firmware without the new capability.
- Add Pico USB console glyph/bitmap diagnostics; panel firmware 0.23 supports
  CGRAM uploads. Physical validation is still required.

## 0.9.1 — faderOS naming

- Adopt faderOS branding in the LCD, web, CLI, documentation and source bundles.
- Provide the `faderOS` executable, preserving `picohost-link` compatibility.
- Build and test the shared control model without Linux adapter dependencies
  using `PICOHOST_BUILD_POSIX=OFF`.
- Record RP2350/W5500 plus additional USB host as the first embedded target.

## 0.9.0 — publication preparation

- Standalone CMake/CTest build, dependency checks, installation and optional Doxygen.
- CLI help/version and opt-in verbose diagnostics.
- English web UI, HTTP errors and current user/development guides.
- Sanitized publication bundle and future GitHub contribution/CI templates.

## 0.8.0 — 2026-10-01

- Three persistent server profiles on USER MENU 1–3; independent adapters,
  endpoints, OBS/vMix/ATEM maps and OBS credentials. Enabled background sessions
  stay connected. Selection waits for the current take/command to finish.
- SYSTEM SETUP / SERVER F4 selects the profile; LCD server pages identify it.
  The web selector uses the same control target and warns about unsaved edits.
- Requests from a stale browser profile are rejected before changing mappings,
  credentials or mixer state. Web rebinding avoids blocking the serial loop.
- SERVER SETUP F6 INFO and web server information expose native model/version
  metadata. ATEM protocol version is explicitly separate from unavailable firmware.
- HDMI exposes advertised output sources; Mini Pro USB offers PGM/PVW/MV only.
  LCD SOURCE cycles and encoder 2 selects; web Salidas offers native choices.
- Web ATEM tabs separate inputs, keys, DSK, outputs, multiview and equipment.
  Multiview includes per-window and all-input SAFE/meters with native confirmation.
  Key/DSK reload now confirms discarding edits consistently with source mappings.
- Regression coverage includes three simultaneous UDP sessions, inactive state
  updates, command isolation, profile persistence and stale web requests.

## 0.7.4 — 2026-10-01

- ATEM multiview: collective input safe-area/meter toggles, leaving PROGRAM and PREVIEW separate; confirmation tracks all targeted windows.

- ATEM video format: capability-driven LCD/web selector with explicit confirmation, native state confirmation and streaming/recording guards.
- ATEM native HDMI/USB output routes, streaming and recording controls.

- LCD DELETE targets the current MP1 still, with a second F2 confirmation.
  Changing MP1 cancels the prompt. Native source tally protects images on PROGRAM
  (fill or alpha); unknown tally blocks deletion of MP1's selected image.
- KEY BUS remains LOW when not delegated; a chosen SPLIT remains LOW until
  SELF/AUTO replaces it, and HIGH while selecting its source.
- ATEM SERVER SETUP gains MVIEW: viewer/window selection, safe areas and audio
  meters, gated by per-window capabilities and confirmed switcher state.

## 0.7.3 — 2026-10-01

- FRAME MEM F2 DELETE opens a slot picker on the delegated KEY row without
  changing MP1. F2 YES explicitly confirms the displayed slot; F1 CANCEL or EXIT
  cancels. Other functions discard the deletion selection.
- Protected MP1 selection, deletion progress/result on the LCD, and a brief beep
  on rejected/unconfirmed deletion. No automatic capture after deletion.
- Physical validation confirms CSTL deletes a still on the Mini Pro.

## 0.7.2 — 2026-10-01

- Explicit console `media-clear SLOT CONFIRM` uses native CSTL, with per-slot
  metadata confirmation and protection for the selected MP1 image.
- Acknowledged but unconfirmed key operations no longer clear all ATEM state and
  reconnect: keep confirmed tallies, release the command and report the failure.
- Log mixer connection changes alongside serial diagnostics to investigate the
  physical report of intermittent panel-wide LED flashes.
- Local peer coverage includes clear success/refusal, media protection and a
  refused key command without reconnect. Physical checks remain pending.

## 0.7.1 — 2026-10-01

- FRAME MEM LCD menu with F1 CAPT, current MP1 slot, capture progress and resulting
  media slot. EXIT closes the page while retaining frame-row delegation.
- Native ATEM Mini PROGRAM capture with metadata confirmation; no automatic image
  selection. Unknown/ambiguous results give an LCD message and brief buzzer pulse.
- Capture confirmation has a five-second deadline independent of other mixer
  controls; no automatic reissue after an unconfirmed result or disconnect.
- Peer coverage includes confirmed capture, a full-pool/no-update response,
  ambiguous updates and unchanged MP1/buses. Physical capture validation pending.

## 0.7.0 — 2026-10-01

- Physical ATEM keyer delegation: LUMA, CHR, PTN and DVE/PinP on DEPTH KEY;
  independent fill/cut selection, SELF/AUTO pairing and premultiplication on CLEAN.
- MAIN MASK, Luma/Pattern/DSK KEY INV, DVE border/shadow and separate DVE crop on SUB MASK,
  with confirmed tallies, off-air configuration guards and no inferred edge modes.
- FRAME MEM1/2 delegates ten still slots per bank to the KEY row; selects MP1 without
  changing keyer routing. Empty slots reject with a brief beep.
- Non-stinger ATEMs gain a second WIPE bank on USER WIPE with the eight remaining
  patterns; DIRECT still uses only verified Sony numbers.
- DME LCD compass and English direction names within the existing LCD protocol.
- Read-only Mini Pro inspection confirms media capabilities; protocol, panel,
  adapter-router and web tests validate the new behaviour. Physical tests pending.

## 0.6.0 — 2026-09-30

- DME PUSH/SQUEEZE uses USER WIPE; MIX shows USER WIPE LOW when DIP is available but inactive.

- ATEM DME: eight PUSH and eight SQUEEZE directions on the keypad, AUTO, T-bar and PREV TRANS; independent duration in frames.
- MIX configuration: USER WIPE toggles native DIP through COL1, with its own frame rate.
- Native stinger activation only on capable ATEM models; Mini Pro remains disabled. No keyer/media-player workaround.
- WIPE BORD, ASPECT (symmetry) and POS (joystick origin), with GLOBAL/CUSTOM and neutral resets; unsupported modifiers remain inactive.
- Native AUTO pause/resume during a transition and immediate CUT completion while running or paused.
- Program/preview sources can change during ATEM AUTO or T-bar transitions without cancelling the take. Completion follows live hardware buses, including external source changes.
- Validated against the local UDP protocol peer and shared control tests; physical validation of this block pending.

## 0.5.1 — 2026-09-30

- Correct ATEM WIPE presets 4/9 after physical operator validation.
- Brief 80 ms buzzer feedback for rejected keypad values, invalid settings and failed commits.
- Buzzer pulses are nonblocking, prioritized over display updates and cleared on link resync.

## 0.5.0 — 2026-09-30

- Native ATEM WIPE for the ten Sony keypad presets, AUTO and manual rehearsal.
- NORM/REV and SOFT (0–100); per-M/E WIPE rates independent of MIX.
- DIRECT rejects unsupported codes at ENTER; failed rate edits retain the active value.
- Validated with the UDP protocol test peer; physical WIPE/operator checks pending.

## 0.4.0 — 2026-09-30

- ATEM manual MIX driven by the calibrated T-bar, with latest-value coalescing and preserved endpoints.
- Hardware-confirmed PREV TRANS on double-click of the transition type; single click returns to normal.
- AUTO is refused while PREV TRANS is active; manual motion keeps AUTO TRANS unlit.
- Physical Mini Pro validation of rehearsal, complete strokes and cancellation.

## 0.3.0 — 2026-09-30

- ATEM fade to black with per-M/E state, frame-rate editing and hardware-confirmed tally.
- ATEM upstream keyer ON, key-only/combined CUT and MIX, and live keyer configuration.
- Shared NEXT TRANS BKGD double-click reset, including coalescing while an order is pending.
- kavtor NEXT TRANS selections wait for confirmed state; keyer availability controls panel feedback.

## 0.2.0 — 2026-09-30

- Runtime selection of OBS, kavtor, vMix and ATEM adapters, with separate endpoints and mappings.
- Capability-aware ATEM buses, M/E delegation and MIX transitions in frames.
- ATEM DSK CUT/AUTO, state-driven tallies and live web configuration.
- Tabbed configuration interface and the accumulated panel/menu improvements.

The native picohost version is defined in `src/link/home.hpp` and shared by
its home LCD, SYSTEM SETUP → INFO and web status. Firmware versions are
independent. Increment the minor version for completed feature blocks and
the patch version for corrective releases.

# User guide

Start with `controller-link --help`. Use a serial device running the replacement
binary-link firmware. The host starts at 9600 baud and negotiates the requested
rate. 38400 baud has been validated on the physical panel. 76800 is experimental
and currently unreliable on the available serial hardware; use 38400.

## Server profiles

USER MENU 1/2/3 selects SERVER 1/2/3 (control IDs 31/30/29). Each profile stores
its protocol, endpoints and mappings independently. Inactive enabled sessions
continue receiving state. Switching waits for pending mixer operations and does
not replay commands. SYSTEM SETUP → SERVER F4 selects the profile being edited
and controlled. The web header selects the same profile. A stale browser tab
must reload before posting edits.

SERVER 1 keeps the original mapping filenames. SERVER 2/3 append `.server2`
and `.server3`. Credentials append `.credentials.json` to the settings base,
including the server suffix. `BKDS_OBS_PASSWORD` applies to SERVER 1 only.
Settings v3 migrates v1/v2 into SERVER 1. Back up settings before downgrading.

## Panel menus

SYSTEM SETUP opens NET, SERVER, CLOCK and INFO. EXIT returns and saves valid
changes. F keys select fields; encoders adjust them. An F key transfers its
field to the keypad; ENTER commits, while another operation cancels pending
editing. ENTER blinks when confirmation is required. Up/down and EXIT use LOW
tally when available. SYSTEM MEM opens protocol-specific SERVER SETUP; INFO
shows the metadata reported by that mixer. ATEM protocol version is not a
firmware version.

## Buses and transitions

PST selects preview; PGM switches program directly. SHIFT is momentary or can
be latched with a double-click. Upper-bank selections blink regardless of SHIFT
state. TRANSITION TYPE selects the active effect and opens its configuration;
opening keypad configuration does not change the on-air transition type.

TRANS RATE selects AUTO, DSK and FTB rates. Units depend on the adapter: OBS
uses milliseconds; ATEM uses its native frame rate counters. MIX/WIPE/DME,
keyers, media capture, multiview and output routing depend on mixer capabilities.
Consult each adapter guide before operating a live program.

## Analog calibration

Use `analog` for a raw/calibrated snapshot and `analog watch` for live samples.
`cal AXIS min|center|max` captures a position (`AXIS`: tbar, x, y). Calibration
format v2 stores a version line, three `low center high` lines and a final
`tbar_margin joystick_dead_zone` line in raw counts. v1 remains readable.

T-bar uses circular 12-bit values; values near 4095 may represent small negative
positions around zero. Define both endpoints together when initially calibrating
an interval crossing zero. Joystick signs follow the panel electronics; mapping
may invert them for individual functions. Samples and their measured endpoints
are specific to each panel; do not copy another panel's calibration blindly.

Manual transitions finish within the last 2% of calibrated travel at either end.
The middle range is unchanged. A 3% departure zone prevents end-stop jitter from
starting another transition immediately after completion. This operating tolerance
is separate from the raw calibration margin stored in the calibration file.

## Security and recovery

The HTTP server currently has no authentication or TLS and binds to all IPv4
interfaces. Enable it only on a trusted control network. Credentials are edited
on the web, never on the LCD. Do not expose the configuration port to the public
internet. Firmware flashing and recovery tools belong to the separate BKDS
firmware workspace; this host repository does not include proprietary Sony ROMs.

## kavtor web workspace

Select kavtor in the active server profile, save the endpoint and wait for the
connection. The Mixing page shows both twelve-position banks. Their final
positions are reserved for the next M/E program; ordinary key/DSK source
selectors exclude these reserved crosspoints. Unassigned inputs cannot be taken.

All four keyers and NEXT TRANSITION arms are available. Transition Preview uses
confirmed server state: its button is enabled only when the server advertises
support and the mixer is idle. During an on-air background transition, the
preview source is marked red because both pictures contribute to program.

In WIPE, the colour picker edits the border colour along with the other wipe
settings. Values are refreshed from the server while preserving unapplied edits.
Builds with Node.js can run the JavaScript state tests in CTest; `make test-web`
also includes them. No Node.js installation is required to run faderOS itself.

### kavtor WIPE modifiers

With kavtor 0.8.9 or newer, the WIPE page applies pattern, direction, softness,
colour border, shadow, repeat count, aspect and position together. Applying
settings does not execute a transition. Widths use resolution-independent units
(4 pixels per unit at 1080 lines). The native application exposes these defaults
under Transition defaults. kavtor-specific procedural patterns use catalogue IDs,
not invented Sony codes. Restart faderOS after updating to load the embedded web
assets; reload the browser afterwards.

See [SuperSource delegation](supersources.md) for kavtor KEY/AUX window routing.

## kavtor native DME

Requires kavtor 0.12.0 and casparMIX 0.5.0 or newer. DME keypad 0 selects MOVE,
1–4 PUSH left/right/top/bottom, 5–8 SLIDE in those directions, and 9 CUBE.
Available choices use LOW; selection uses HIGH. AUTO uses TRANS RATE in frames.
T-bar supports the same choices, including preview and existing M/E pickup.
NORM/REV reverses cube/directional effects. MOVE always follows the selected
program-to-preview geometry; reverse T-bar movement rewinds that geometry.

MOVE snapshots source identities and box geometry without changing shared
SuperSource channels. Inputs absent from one side fade; ambiguous repeated inputs
are not guessed. NEXT TRANSITION must contain BKGD only. Existing keys and DSKs
remain outside this background transition. The panel firmware is unchanged.

DME has one shortcut page: 0 MOVE, 1–4 PUSH, 5–8 SLIDE, 9 CUBE.
USER WIPE is unavailable in DME. DIRECT selects advertised Sony codes or local
extended effects: 12 ZOOM, 13 PAGE TURN, 14 PAGE ROLL. Each M/E retains its choice.
WIPE keeps the ten printed presets; USER WIPE selects configured stingers only.
DIRECT and USER WIPE return to printed presets when switched off.

MODFY opens the prepared effect's background menu. F1 AUX selects any available
input; F3 COLOR filters AUX to configured Matte colour generators. F2 BLACK resets
the background. F1/F3 toggle editing, EXIT closes it, and double-click MODFY resets
to BLACK without opening a menu. AUX feedback uses LOW/blink, matching SuperSource
selection. The last AUX position selects the next M/E programme, without SHIFT.
Backgrounds are silent, programme takes are snapshots, and live background edits
apply only to transition preview. The page's backside is a separate material.

## Native MIX preparation

With kavtor 0.17.0 and casparMIX 0.11.0, MIX shortcuts are 1 MIX, 2 VFADE,
3 FADECUT, 4 CUTFADE, 5 DIP, 6 NAM and 7 SUPER MIX. Only supported choices light.
NAM chooses the complete pixel from the higher-luminance video signal, with both
signals at full gain at midpoint. SUPER MIX adds the two independently weighted
video signals, clamped to the output range. Audio remains an ordinary crossfade.

For DIP, MODFY edits the intermediate colour as RGB 0–255. For SUPER MIX it edits
A/B video gain at midpoint as 0–100%. Encoders prepare each field; its F key lends
the keypad to that field. ENTER commits a numeric draft, pressing the same F key
again cancels it. F6 restores black or 100%/100%; double-click MODFY resets without
opening the menu. Programme transitions capture their settings at preparation;
TRANSITION PREVIEW can update them while running. No panel flash is required.

ATEM WIPE DIRECT additionally accepts 800–817 for the native ATEM pattern enum
0–17. These local codes are explicitly not Sony catalogue identifiers. The ten
printed shortcuts retain their Sony mapping. ATEM DME DIRECT accepts Sony
2601–2608 (Slide/Push) and 2621–2628 (Squeeze); USER WIPE is unused.

## kavtor native key controls

With kavtor 0.19 and casparMIX 0.13, delegate the USK or DSK and choose LUMA,
LINEAR or CHR. LUMA is LOW when available and HIGH when selected. MAIN MASK
toggles its prepared rectangle; KEY INV toggles key alpha, with LOW indicating
inversion. The application processing dialog prepares black/white LUMA
thresholds and independent MAIN MASK inversion. Settings are separate for each
key/DSK and persisted only after renderer acknowledgement. Unavailable modes
are rejected rather than substituted. No Sony panel firmware update is needed.

## Retrying rejected numeric entry

A rejected numeric value stays visible after the buzzer. The first subsequent
digit replaces that rejected draft; following digits append normally. CLEAR is
optional. This applies to WIPE/DME DIRECT, transition rates, modifier fields and
SYSTEM SETUP numeric editing. Invalid setup values cannot be committed by ENTER
or truncated into an octet/port. Changing function still cancels the draft.

## GLOBAL/CUSTOM DME backgrounds

kavtor 0.20 uses one GLOBAL background unless a preset has a CUSTOM assignment.
In MODFY: F1 AUX, F2 BLACK, F3 COLOR, F4 GLOBAL, F5 CUSTOM, F6 RSTGL. Source
assignment edits the selected scope. Switching back to GLOBAL retains the saved
CUSTOM value; RSTGL copies the current GLOBAL into CUSTOM. Changing GLOBAL does
not overwrite customised presets. Programme takes retain captured preparation;
private preview can update live. The application has a common background row
and per-effect inheritance choices.

MODFY uses HIGH while DIP/SUPER MIX editing is open, LOW when the applicable
prepared values are non-default, and OFF on ordinary MIX modes without those
parameters. Unsupported/irrelevant DME preparation does not light a MIX modifier.

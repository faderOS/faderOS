# ATEM adapter

Native C++ adapter over UDP 9910. No Python bridge or ATEM Software Control
process is needed at runtime. Physical validation: ATEM Mini Pro at
192.168.1.92, protocol 2.32, 1080p25 (2026-09-29).

## Running

```
make -C picohost link
cd picohost
build/picohost-link --port /dev/ttyUSB0 --baud 38400 --web 8088
```

In faderOS → Servidor choose ATEM, its IPv4 address and port 9910 (or 0 for
the default), then save. The saved protocol selects the adapter on subsequent
starts. `--atem` can override the initial protocol, but does not override the
saved ATEM endpoint. OBS, kavtor and vMix retain their own settings.

UTILITY 1–4 delegates the panel to M/E 1–4 without sending a take command.
Absent M/Es stay dark and ignore presses. The selected M/E is HIGH and other
available M/Es are LOW. In Protocolos → ATEM choose the M/E whose assignments
you want to edit; this does not delegate the panel or affect program.
The available M/Es, upstream keyer counts, DSKs, AUX outputs, DVE blocks and
source catalogue come from the device, not the model name. Every M/E has its
own received program, preview, transition and rate state. Hardware source IDs
are preserved even if names change. Source availability for the selected M/E
comes from `InPr`; output/status-only sources are not offered on its buses.

There are 24 panel positions (12 normal + 12 SHIFT), not a 24-source device
limit. Position 12 in both banks is reserved for the next M/E program
(`10010 + 10 × next zero-based M/E index`), subject to the advertised
availability mask. In the final M/E, or if the route is unavailable, it is
disabled. The cascade tally is steady regardless of SHIFT. Any advertised source available to that M/E can occupy a panel slot.
Missing sources stay unassigned operationally; there is no substitution.

Assignments and M/E selection are saved atomically in `atem-mappings.json`;
`--atem-mappings FILE` selects another profile. The initial profile is empty. A local profile has been prepared for this Mini
Pro: CAM1, CAM2, CAM3, CAM4, BLK, COL1, COL2, BARS, MP1; all other positions empty.
The web's fill button explicitly proposes the first 22 available source IDs;
review them before saving. Profile schema:

```
{"version":2,"me":0,"sources":["2001","2002","", ... 24 entries total ...],"meSources":{}}
```

`me` is the zero-based startup delegation. `sources` are shared assignments;
`meSources` optionally maps zero-based M/E index strings to individual 24-slot
arrays. The web can edit common assignments or a custom row for one M/E.
This permits e.g. slot 8 on M/E 1 to select M/E 4 program if its `InPr` mask
allows it. Source IDs are decimal strings; empty strings mean no assignment.
Version 1 profiles are read and upgraded on save; positions 12/24 are reserved. Repeated source IDs are rejected. Changes are refused during a
command or transition and protected by a web revision check. UTILITY changes only local delegation, not the saved startup M/E. If a profile
from a larger switcher refers to a startup M/E absent on the connected device,
local delegation falls back to M/E 1; no bus command is sent.

## Implemented

- PST selection and PGM hot punch, with tallies confirmed by the ATEM state.
- CUT and AUTO MIX/WIPE respect NEXT TRANS on the chosen M/E: BKGD, upstream
  keyers, or both. AUTO explicitly selects the requested MIX/WIPE style and supplies its duration. It is refused while PREV TRANS is enabled. Key-only transitions leave program and preview buses intact. Downstream-keyer TIE settings are respected and can be configured on the DSK web page.
- DSK ON cuts the selected DSK on/off; DSK MIX performs its native AUTO.
  DSK ON follows on-air state and DSK MIX lights only during the transition.
  DSK PVW selects DSK 2 by hold/double-click only when the device has it.
- TRANS RATE uses **frames**, 1–250, and synchronizes external MIX, WIPE and DSK rate changes.
- Incoming transition and preview-on-program state drives tallies. Auto/manual
  feedback uses the reported frames remaining, including external operation.
- Runtime adapter switching through the common router; separate web profile.
- Reliable sequencing, 15-bit wrap, duplicate suppression, gap retransmission
  requests and bounded retransmissions with the original command packet ID.
  Lost sessions discard pending commands: CUT/AUTO are never replayed in a
  new session. Commands remain busy until ACK and observed result agree.

## Upstream keys and NEXT TRANS

KEY1 ON / KEY2 ON cut the corresponding upstream key on/off on the delegated
M/E; absent keyers are ignored and their lamps remain off. NEXT TRANS BKGD,
KEY1 and KEY2 toggle the armed layers. Chords update multiple arms in one
selection. At least one layer stays armed; clearing the final layer selects
BKGD. Double-click BKGD selects background alone and clears all armed upstream
keys, including keys 3/4 armed from ATEM software. It never changes on-air
state by itself. A second click can supersede an arming command awaiting
confirmation. ON buttons ignore chords to avoid unintended on-air changes.

CUT and AUTO now preserve these arms. A key-only take toggles the selected
keys without swapping buses; BKGD plus keys changes both. Completion waits
for the expected on-air keys as well as buses. External `TrSS` / `KeOn` state
controls the feedback; keyer counts come from `_MeC` and storage is per M/E.
The panel currently exposes the first two keyers; additional delegation is
pending. Wire transition-layer selection represents four keys.

Protocolos → ATEM → Keyers lists every detected keyer by M/E. Fill/key and
type (Luma, Chroma, Pattern, DVE when available) can be edited off air. Luma
supports premultiplication, clip/gain (raw 0–1000) and inversion. Chroma,
pattern and DVE parameters retain their mixer settings; their advanced UI is
pending. Mask and fly settings are preserved. Editing another M/E's keyer
does not delegate the desk. State confirmation and revision checks protect
against stale web edits and reconnects.

Physical Mini Pro test passed (2026-09-30): existing DVE keyer configuration,
ON cut in/out, KEY1-only CUT/MIX, combined BKGD+KEY1 CUT and coalesced reset.
Original keyer configuration, buses, rate and armed layers were restored.

## Fade to black

FADE TO BLACK operates the delegated M/E, independently of the selected
transition type. TRANS RATE → FTB sets 1–250 frames. Rates synchronize from
ATEM, including external changes and M/E delegation. `FtbC` sets duration;
`FtbA` triggers the native fade. `FtbP` and `FtbS` confirm rate/state.

The FTB lamp is HIGH throughout the fade and while fully black; it goes off
when the return to picture completes. AUTO TRANS is not illuminated by FTB.
Repeated FTB presses during a fade are ignored. An ACK plus observed start
releases the command queue; no optimistic tally or replay after reconnect.
The FTB capability is separate from upstream keyer support.

`build/atem-probe HOST PORT PROFILE ftb` tests entry and return, frame rate
synchronization and restores the original rate/black state without changing
buses. Use only on test signals: this visibly fades the output.
Physical test passed on the Mini Pro on 2026-09-30 at 25 frames; original
rate, black state and buses were restored.

## DSK configuration

Protocolos → ATEM → DSK lists detected downstream keyers. The desk exposes
DSK 1 and 2; the web can configure every reported DSK. Select fill/key from the
advertised input catalogue, duration in frames, premultiplication, clip/gain
(raw 0–1000), inversion and TIE. These are live ATEM settings, not a profile
reapplied at connection time. Mask settings are preserved.

Configuration is accepted only while the DSK is off air and idle. A revision
check rejects stale edits, including across reconnects. The page waits for
ATEM state confirmation before reporting success. DSK mixing releases the
command queue after its start is confirmed; the actual incoming DSK state
continues to drive tallies while the buses remain usable.

`DskB`, `DskP` and `DskS` supply state. `CDsL`, `CDsR`, `CDsF`, `CDsC`, `CDsT`
and `CDsG` supply commands; `DDsA` uses the version-dependent layout introduced
in protocol 2.29, with explicit direction on newer switchers. Peer tests cover
one modern and two legacy DSKs, rate/configuration confirmation, both AUTO
directions, invalid sources, stale edits and off-air protection.

## Scope and capabilities

`_ver`, `_top`, `_MeC`, `_MvC` and all `InPr` entries are decoded. Topology
layout changes in protocol 2.30 are handled. Unknown fields are ignored;
reported hardware capabilities do **not** mean all corresponding controls
are implemented. The supported operations above work on the selected M/E;
Advanced keyer parameter editors, media upload and general routing remain
future increments. WIPE/DVE transitions, T-bar and still selection are supported
as described below. The native state uses dynamic M/E and source
containers. The source-to-M/E mask is eight bits in the wire protocol.

Shared BKGD reset also works through the kavtor adapter using its existing
NEXT layer toggle commands. It arms new layers before disarming old ones,
waits for server state and coalesces a reset requested while the first click
is pending. Reconnects discard pending changes.

## Verification

```
make -C picohost test-atem test-web test-adapter-router test-transitions test-vmix
```

The ATEM peer tests four M/Es, six USKs per M/E and 46 sources, addressing M/E
3 rather than assuming M/E 1. It covers UTILITY delegation, per-M/E assignments,
SHIFT-independent next-M/E re-entry and disabled re-entry on the final M/E. It simulates lost command/state/ACK packets,
duplicated and reordered state, plus reconnect after a CUT with no reply.
Unit tests cover malformed packets, session isolation and sequence wrap.
Web tests cover persistence, stale revisions, input validation and independent
vMix/ATEM files.

`build/atem-probe HOST PORT PROFILE` only reads state. Adding `exercise` changes
buses, runs MIX and restores the initial buses and mix rate on success; use
only with test signals. It writes its own supplied profile. Real Mini Pro
validation passed PST, hot punch without changing PST, CUT swap, MIX/tallies,
and original-bus/rate restoration. Larger hardware has mock coverage only.

## References

- https://docs.openswitcher.org/udptransport.html
- https://docs.openswitcher.org/fields/hardware.html
- https://docs.openswitcher.org/fields/switcher.html
- https://docs.openswitcher.org/commands/switcher.html
- https://github.com/Sofie-Automation/sofie-atem-connection

The more recent Sofie implementation clarifies the 15-bit packet counter,
retransmission ID at header offset 6, version-dependent topology and input
availability fields. OpenSwitcher's speculative input fields are not used
as a substitute for the verified availability mask.

Source IDs for M/E re-entry are also documented by the ATEM Wireshark dissector:
https://github.com/peschuster/wireshark-atem-dissector/blob/master/atem_dissector.lua

`build/atem-probe HOST PORT PROFILE dsk-physical` requires DSK 1 initially off
air. It reapplies unchanged configuration, checks CUT and AUTO in both
directions, then verifies the original DSK settings and buses are preserved.

Physical DSK validation passed on the Mini Pro (2026-09-30): unchanged
MP1/MP1K configuration, CUT on/off, AUTO on/off at 25 frames, and preservation
of the original off-air state and both buses. Panel button validation remains
a user acceptance step.

Upstream keyer base and luma layouts were checked against:
https://github.com/Sofie-Automation/sofie-atem-connection/blob/main/src/commands/MixEffects/Key/MixEffectKeyPropertiesGetCommand.ts
https://github.com/Sofie-Automation/sofie-atem-connection/blob/main/src/commands/MixEffects/Key/MixEffectKeyLumaCommand.ts

## Manual MIX and PREV TRANS

The calibrated T-bar drives native `CTPs` positions (0–10000). Begin at either
physical endpoint; a complete stroke takes the armed layers, and the return
stroke performs the next take. Returning to the starting endpoint cancels the
unfinished take. Samples are coalesced to the latest value, limited to one new
position per 10 ms and acknowledged by the UDP transport; terminal samples
cannot be overwritten. Holding a partial mix is supported. A lost session drops
pending motion and requires endpoint reacquisition; it does not replay a take.

Double-click MIX activates native PREV TRANS; the selected type lamp blinks
only after confirmed `TrPr`/`TsPr` state. A single type press disables it.
ATEM rehearsal uses the T-bar on preview: program buses and on-air keys remain
unchanged. AUTO is refused while rehearsal is active; CUT and hot punch retain
their explicit on-air meaning. Type/preview changes and M/E delegation are
refused during an active manual stroke. MIX and WIPE are implemented.
AUTO TRANS stays unlit during manual movement, including rehearsal.

Validated on the physical Mini Pro: rehearsal leaves program/preview intact,
two complete strokes restore buses, return-to-start cancels without a take,
and key-only manual takes preserve background. Original arms/preview mode are
restored by the probe. Operator validation using the Sony lever remains needed.

Reference: [native position command](https://github.com/Sofie-Automation/sofie-atem-connection/blob/main/src/commands/MixEffects/Transition/TransitionPositionCommand.ts)
and [preview command](https://github.com/Sofie-Automation/sofie-atem-connection/blob/main/src/commands/MixEffects/Transition/TransitionPreviewCommand.ts).

## WIPE (0.5.0)

WIPE Transition Type opens the keypad without taking a source. The ten presets
use Sony codes, translated to ATEM native patterns:

| Key | Sony code | ATEM pattern |
| --- | --- | --- |
| 0 | 23 | 6 Diamond iris |
| 1 | 5 | 8 Top-left box |
| 2 | 21 | 5 Rectangle iris |
| 3 | 24 | 7 Circle iris |
| 4 | 18 | 3 Vertical barn door (opens horizontally) |
| 5 | 9 | 16 Top-left diagonal |
| 6 | 6 | 9 Top-right box |
| 7 | 1 | 0 Left-to-right bar |
| 8 | 3 | 1 Top-to-bottom bar |
| 9 | 17 | 2 Horizontal barn door (opens vertically) |

DIRECT accepts these ten mapped codes. Additional code/pattern assignments
remain pending until the Sony pictogram list is supplied (ATEM exposes native patterns 0–17). Other codes are rejected at ENTER and do not replace the committed code.
Operator validation confirmed the other patterns; presets 4/9 are swapped in
0.5.1 to correct their opening axes. The corrected pair awaits rechecking.

AUTO supplies WIPE rate, pattern, reverse, softness and disables native flip-flop;
NORM/REV alternation remains in the shared panel logic. Manual WIPE uses the same
parameters and PREV TRANS behaviour as MIX. SOFT 0–100 maps to native border
softness 0–10000, retaining the shared GLOBAL/CUSTOM editing behaviour. Unedited
border width/fill, symmetry and origin retain their ATEM settings. MIX and WIPE
rates are read independently from `TMxP`/`TWpP`; TRANS RATE writes the armed type.
Keypad-only preparation never arms Transition Type. See the 0.6.0 additions below.

Tests cover both directions of all ten presets, softness conversion, parameter
mask preservation, separate rates, invalid DIRECT/rates, manual rehearsal and
existing buses/keyers/DSKs. No physical WIPE take was performed for this release.

Reference: [WIPE command and state](https://github.com/Sofie-Automation/sofie-atem-connection/blob/main/src/commands/MixEffects/Transition/TransitionWipeCommand.ts)
and [native pattern enum](https://github.com/Sofie-Automation/sofie-atem-connection/blob/main/src/enums/index.ts).


## DME, DIP and WIPE modifiers (0.6.0)

DME Transition Type opens the DME keypad. The eight direction keys follow their
physical layout: 7/8/9 at the top, 4/6 at the sides and 1/2/3 at the bottom.
USER WIPE toggles PUSH/SQUEEZE; its lamp is LOW for PUSH and HIGH for SQUEEZE. Keys 0 and 5 are unsupported
and gives a brief buzzer indication. WIPE on the keypad blinks to identify the
DME page. AUTO, manual T-bar and PREV TRANS use the prepared direction;
NORM/REV also applies. Native DVE durations are read and edited independently
from MIX, WIPE and DIP. DME is unavailable when the ATEM reports no DVE.

Selecting MIX Transition Type opens its keypad configuration. USER WIPE toggles
between ordinary MIX and native DIP through COL1; its lamp is LOW when available and HIGH for DIP.
DIP has a separate duration in frames and supports AUTO, T-bar and PREV TRANS.
In the WIPE keypad page, USER WIPE retains its stinger meaning. It activates the
single native stinger configured externally, only when the hardware reports
stinger capability. Its configured timing is preserved. Mini Pro reports zero
stingers and rejects this selection. Keyer/media-player imitations are outside
faderOS support; any external macros remain the operator's responsibility.

Native WIPE modifiers use the common LCD editor, with GLOBAL/CUSTOM, F1 keypad
editing committed at ENTER, and double-click reset:

- BORD: border width 0–100; neutral value 0. The ATEM border-fill source is retained.
- ASPECT: symmetry 0–100; neutral value 50.
- POS: origin controlled by the joystick, with F1 centering; neutral position 500/500.

Settings are prepared locally and applied with the next WIPE; preparing a
modifier does not change an ongoing take. Untouched native fields are preserved.
MODFY, MULTI and ROTATION remain inactive because these controls have no native
counterpart in the consulted WIPE command fields.

Program and preview buttons remain usable during an ATEM transition, including
AUTO, T-bar and rehearsal. Source orders share the reliable UDP transport without
replacing the pending transition operation. After a take has started, its native terminal transition state confirms completion, even if bus updates arrive in separate packets or sources are changed from another ATEM client. Tallies keep following the hardware buses.
AUTO during a transition sends the native pause/resume order without rewriting its type or parameters. CUT completes the transition immediately to preview, whether running or paused. Pausing does not trip the automatic-operation timeout; UDP loss and session failures still remain monitored.

The local UDP peer tests all 16 DVE combinations, DIP through COL1, independent
rates, native stinger capability guards, parameter masks, WIPE modifiers and
source changes during AUTO/manual/rehearsal. Physical validation of this new
block remains pending; no firmware flash is required.

References: [DVE commands and state](https://github.com/Sofie-Automation/sofie-atem-connection/blob/main/src/commands/MixEffects/Transition/TransitionDVECommand.ts),
[DIP commands and state](https://github.com/Sofie-Automation/sofie-atem-connection/blob/main/src/commands/MixEffects/Transition/TransitionDipCommand.ts).


## Physical key controls and frame memories (0.7.0)

The selected KEY 1 / KEY 2 / DSK delegation owns the KEY CONTROL controls.
Unavailable delegations remain off. Configuration commands require the keyer
(or DSK) off air and the adapter idle; rejection gives a brief buzzer pulse.
The native state, rather than the button press, confirms tallies.

- LUM selects ATEM's **Luma** engine, preserving fill and cut source assignments.
  LINEAL is not a separate ATEM type and remains unassigned.
- CLEAN toggles premultiplication within Luma; LOW indicates it is enabled.
  Use it for premultiplied fill/alpha, and leave it off for straight alpha.
- CHR selects Chroma and PTN selects Pattern on upstream keyers only.
- DEPTH KEY selects the native DVE/PinP type on a capable upstream keyer.
  PROC KEY remains free.
- MAIN MASK toggles the native upstream or downstream key mask, preserving
  the four existing bounds. KEY INV toggles Luma, Pattern or DSK inversion; it is rejected
  for Chroma and DVE instead of changing an unrelated setting.
- BORD and SHDW toggle native DVE border and shadow, preserving width, colour,
  softness, lighting, position and size. Their LOW tallies follow ATEM state.
- SUB MASK toggles the separate native DVE crop mask only in DVE mode,
  preserving its existing bounds. It is unavailable for other key types and DSK.
- DROP BORD and OUTLINE have no assignment in this increment.
  Editors for mask bounds and detailed DVE/key parameters remain pending;
  configure these values in ATEM Software Control or OpenSwitcher meanwhile.

KEY SOURCE / FILL KEY BUS arms selection of fill from the KEY row. SPLIT arms
selection of the cut/alpha source. SELF pairs fill with itself; AUTO selects
the associated Media Player Key for a Media Player Fill (otherwise self).
SHIFT selects the upper source bank; upper-bank selected sources blink
regardless of the currently selected bank. The existing web keyer/DSK forms can independently assign fill and cut.

FRAME MEM1 selects image slots **1–10** and FRAME MEM2 slots **11–20**.
The first ten buttons in the KEY row choose their respective slot; SHIFT
does not alter this numbering. Pressing a frame-bank button only delegates
the row; it does not select an image or change any keyer routing. Empty and
out-of-range slots are rejected. Press the active bank again while its LCD page is open to release it.
EXIT closes the page and retains the frame-row delegation; pressing its bank
button again reopens the page.

Image selection changes **MP1**. To use the image in a keyer or DSK, assign
MP1 as fill and MP1 Key as cut separately. Both share the same single player
on Mini Pro: changing its still affects every on-air use of that player.
Image selection never automatically puts a keyer on air. Media upload is not
implemented; load images with ATEM Software Control or OpenSwitcher.

Read-only Mini Pro inspection confirmed one player, twenty still slots and
MP1/MP1 Key IDs 3010/3011. The local protocol peer validates still confirmation,
empty-slot rejection, paired fill/key selection, all four key types, DVE
edge command masks, primary mask/inversion and off-air protection. Physical
button validation of this increment remains pending; no flash is required.

Sources: [upstream key properties](https://github.com/Sofie-Automation/sofie-atem-connection/blob/main/src/commands/MixEffects/Key/MixEffectKeyPropertiesGetCommand.ts),
[DVE settings](https://github.com/Sofie-Automation/sofie-atem-connection/blob/main/src/commands/MixEffects/Key/MixEffectKeyDVECommand.ts),
[media player selection](https://github.com/Sofie-Automation/sofie-atem-connection/blob/main/src/commands/Media/MediaPlayerSourceCommand.ts).

## Eight additional WIPE presets (0.7.0)

On an ATEM without native stingers, USER WIPE switches the WIPE keypad bank:
LOW = the ten printed Sony presets; HIGH = the eight additional native patterns.
It continues to control DIP in MIX and PUSH/SQUEEZE in DME.
On a stinger-capable ATEM the previous native USER WIPE stinger behaviour is
retained; the extra-bank mapping is restricted to non-stinger models.

| Upper-bank key | Native pattern | Description |
| --- | --- | --- |
| 0 | 4 | Four corners |
| 1 | 10 | Bottom-right box |
| 2 | 11 | Bottom-left box |
| 3 | 12 | Top-centre box |
| 4 | 13 | Right-centre box |
| 5 | 14 | Bottom-centre box |
| 6 | 15 | Left-centre box |
| 7 | 17 | Top-right diagonal |

Keys 8/9 in this bank are invalid and beep without changing the selected pattern.
These presets support AUTO, native manual transition, REV and SOFT. The host
uses internal pattern tokens, **not invented Sony DIRECT codes**. Modifier
menus identify these presets as B2-0 through B2-7; the seven-segment display
shows the keypad digit. DIRECT retains the ten confirmed Sony codes until
the additional Sony pictogram numbers are supplied. Returning to the lower
bank restores its last preset. All eighteen native patterns have peer coverage.

DME direction is shown using English names and a large 15x16-pixel arrow
in three columns and both LCD rows. Six CGRAM slots are uploaded through the
existing panel firmware 0.23+ graphics protocol; no new flash update is needed.
Direction follows keypad positions 7/8/9, 4/6, 1/2/3. The upward arrow was
validated on the physical panel; the remaining directions need operator checks.


## Native PROGRAM still capture (0.7.1)

FRAME MEM1/2 opens an English LCD page. F1 **CAPT** captures PROGRAM into the
ATEM media pool. The title shows CAPTURING PROGRAM until media metadata changes,
then CAPTURED SLOT NN using one-based pool numbering. MP1's selection is shown
separately; a capture does not automatically select or route the new image.
Opening SETUP or another control menu relinquishes the LCD but retains the
frame-row bank until another source delegation is selected. EXIT also closes
only the page. The former bank-selection and KEY-row image-selection controls
remain available independently of the LCD page.

The capture command has no target index. faderOS compares the complete media
metadata received before sending Capt with subsequent MPfe updates. One changed,
occupied slot confirms the destination; multiple changed slots are ambiguous.
Without an identifiable update within five seconds, CAPTURE NOT CONFIRMED and
one short beep report the unresolved result. Transport acknowledgement alone
is insufficient. The wait releases the mixer command queue after acknowledgement,
so ordinary controls continue to work; a second capture is rejected while waiting.

A full pool is not blocked by the host: the native switcher determines what
happens. faderOS does not clear existing slots to make space. Physical validation confirms that the Mini Pro refuses further captures when
all twenty slots are occupied. The current capture capability is
advertised for detected ATEM Mini models; other models require verification.
The host never automatically reissues Capt after an unconfirmed result or a
reconnect. Normal UDP retransmission retains the same packet identity.

Local tests cover a confirmed capture, a full-pool/no-update case, ambiguous
updates, unchanged MP1 and buses, menu focus/navigation and queue availability
while waiting. These are protocol-peer checks, not a physical capture test.

Source: [native Capt command](https://github.com/Sofie-Automation/sofie-atem-connection/blob/main/src/commands/Media/MediaPoolCaptureStillCommand.ts).


## Clearing a still (0.7.2)

ATEM has a per-slot native clear command: **CSTL**, four payload bytes, zero-based
slot at byte 0 and three reserved zero bytes. This differs from Capt, whose
payload is empty and which offers no destination or overwrite flag.

In the picohost console, `media-clear 8 CONFIRM` deletes pool slot **08** (one-based
operator numbering). This is an explicit deletion, not a replacement/capture.
Only issue it for a disposable image. The current MP1 selection is protected when its fill or alpha is on PROGRAM;
an unknown native source tally also blocks deletion of that selected image. The host currently allows clearing
only mixers reporting one media player, since additional players' selections
are not tracked. Empty slots, unknown metadata, active operations/transitions
and concurrent captures/clears reject the request.

Console output progresses from PENDING to CONFIRMED only after MPfe reports
that exact slot empty. ACK alone is insufficient; after five seconds the result
is NOT CONFIRMED. The command is not automatically reissued, and PROGRAM,
PREVIEW and MP1 selection are not changed. Slot selection is blocked for the
slot being cleared while confirmation is pending. Normal controls remain usable.
A subsequent F1 CAPT makes a separate capture; its actual destination is still
reported from metadata, not assumed to be the cleared slot.

Physical validation by the operator confirms CSTL deletes the selected pool slot. Local protocol-peer tests cover
confirmed deletion, a refused deletion, selected-slot/bounds/empty-slot guards,
and unchanged buses and MP1. Physical deletion/next capture remains to validate.

Source: [native CSTL command](https://github.com/Sofie-Automation/sofie-atem-connection/blob/main/src/commands/Media/MediaPoolClearStillCommand.ts).

## Diagnosing tally interruptions (0.7.2)

faderOS now logs mixer connection/message changes separately from the serial
LINK LOST/SYNC messages. An acknowledged key command whose resulting state is
not confirmed after 2.5 seconds reports ATEM COMMAND NOT CONFIRMED with the M/E,
key index and operation. It releases the operation without discarding measured
state or restarting the UDP session. The confirmed tallies remain authoritative;
no success or optimistic state is manufactured. Transport failures still
reconnect normally. This is covered with a peer that refuses MAIN MASK while
continuing to acknowledge commands and publish state.

The physical report of all LEDs flashing has not yet been reproduced or proven
resolved: the supplied serial log has no LINK LOST/SYNC and no UART errors.
Use the new mixer messages to identify whether a network reconnect coincides
with the flash before changing validated firmware lamp initialization.


## LCD deletion (0.7.3)

FRAME MEM1/2 now shows F1 CAPT and F2 DELETE. Press DELETE, then choose a slot
using the delegated KEY row (01–10 or 11–20). This choice does not change MP1.
The LCD shows DELETE SLOT NN? and F2 YES. A separate press of F2 sends CSTL;
F1 CANCEL or EXIT cancels and returns to the frame-memory page. Other functions
cancel the deletion selection. Simultaneous soft-key/source presses cannot
confirm. The MP1-selected slot is identified as protected and has no YES action.
The LCD reports DELETING SLOT NN, DELETED SLOT NN or DELETE NOT CONFIRMED based
on switcher metadata; rejected/unconfirmed commands give a short beep. A delete
does not automatically make a new capture. The new LCD workflow has local
control tests; physical LCD validation remains pending.


## Current LCD deletion and source feedback (0.7.4)

The 0.7.3 slot picker is superseded. F2 DELETE now opens DELETE SLOT NN? for
MP1's current still. A second F2 YES confirms that exact image. F1 CANCEL or
EXIT returns to the frame page. Choosing another image, changing delegation or
receiving a different MP1 selection from another client cancels the prompt.
Native TlSr program tally for MP1 Fill (3010) and Key (3011) protects on-air
images, including key/DSK use. Preview tally alone does not prevent deletion.
If either source tally is unknown, deletion of MP1's selected image is blocked.
The backend rechecks the protection on request and before sending CSTL.

KEY BUS indicates the chosen fill mode with LOW and source-edit delegation
with HIGH. SPLIT is remembered per key/DSK delegation: LOW while chosen and
HIGH during cut-source selection. Choosing SELF/AUTO clears that SPLIT choice.
MATTE and a full persistent SELF/AUTO mode implementation remain pending.

## ATEM SERVER SETUP / MVIEW (0.7.4)

SYSTEM MEM opens ATEM SERVER SETUP; F1 MVIEW opens multiview settings.
F1 cycles multiviewers, F2 cycles existing windows. Encoders 1/2 select the same
viewer/window; UP/DOWN step through windows. F3 SAFE and F4 METERS toggle the
selected window's native settings. LOW means off, HIGH means on; absent or
unknown options are omitted. EXIT returns to ATEM SERVER SETUP.

MvIn support flags determine which window offers safe areas or meters;
SaMw and VuMC supply their actual values. No PGM/PVW window positions or number
of viewers is assumed. Commands use SaMw and VuMS, with state confirmation.
Live changes made in ATEM Software Control/OpenSwitcher refresh the LCD.
These settings affect multiview display only. Layout/routing and other display
options are not yet exposed by this page.

Primary protocol references: [source tally](https://github.com/Sofie-Automation/sofie-atem-connection/blob/main/src/commands/TallyBySourceCommand.ts),
[multiview window capabilities](https://github.com/Sofie-Automation/sofie-atem-connection/blob/main/src/commands/Settings/MultiViewerSourceCommand.ts),
[safe areas](https://github.com/Sofie-Automation/sofie-atem-connection/blob/main/src/commands/Settings/MultiViewerWindowSafeAreaCommand.ts),
[audio meters](https://github.com/Sofie-Automation/sofie-atem-connection/blob/main/src/commands/Settings/MultiViewerWindowVuMeterCommand.ts).

### Video standard and outputs

ATEM SERVER SETUP exposes FORMAT (F3). F3 cycles modes and encoder 3 selects in either direction, only using modes advertised
by `_VMC`; APPLY followed by CONFIRM sends `CVdM`. `VidM` is the authority for
the active format. EXIT cancels a pending confirmation. Changing format may
clear the media pool and interrupt video output. Requests are rejected during
transitions, recording or streaming, and if the current mode changed since
confirmation. The web ATEM page provides the same supported-mode selector and
explicit confirmation; no live video standard was changed during development.

OUTPUT exposes advertised native AUX sources. F1 cycles routes; F2 SOURCE
cycles choices and encoder 2 selects them in either direction. Mini Pro devices
with two routes label these HDMI/USB. HDMI offers native source choices; USB is
restricted to PROGRAM, PREVIEW and MULTIVIEW as physically confirmed by the user. ENABLE EDITOR toggles native streaming;
ENABLE DME toggles native recording, with state confirmed by StRS/RTMS.

In MVIEW, F5 INSAFE and F6 IN VU toggle all supported input windows of the
selected viewer together. If any eligible window is off, the action turns them
all on; if all are on, it turns them all off. PROGRAM/PREVIEW sources are
excluded using the source IDs reported by MvIn, independent of window position.
F3 SAFE/F4 METERS retain individual window control. A grouped operation is
confirmed only when every targeted window reports the requested state.

### Physical transition timing at 1080p50

Measured on the Mini Pro on 2026-10-01 using native commands and switcher
state updates. With value 25, MIX AUTO took 1001 ms, DSK AUTO 1000 ms, and
FTB 1000 ms (command to transition completion). Therefore native transition
rate units use a 25 Hz timebase in 1080p50, not the 50 fps output cadence.
faderOS forwards these native values unchanged for AUTO/DSK/FTB; it does not
convert them to milliseconds or double them when the video mode changes.
Do not infer duration from output fps alone, or extrapolate this measurement
to other video standards without verification. The timing probe restored
buses, next transition, transition preview, DSK state/tie and original rates.


### Web equipment and multiview (0.8.0)

ATEM tabs separate Entradas, DSK, Keyers, Salidas, Multiview and Equipo.
Equipo contains the advertised video format selector and explicit confirmation.
Multiview exposes the same SAFE/meters operations as the LCD: individual windows
and all supported input windows, excluding PROGRAM/PREVIEW from grouped changes.
Pending operations report success only after matching native state; unavailable
capabilities remain disabled. The interface has local validation; physical web
multiview/output tests remain pending.

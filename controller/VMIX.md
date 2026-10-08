# vMix — basic main mix adapter

Run from `picohost`:

```sh
make link
build/picohost-link --port /dev/ttyUSB0 --baud 38400 --web 8088
```

In the web host settings choose VMIX, address `192.168.122.202`, port **8099**
(the TCP API; 8088 is the HTTP interface). Save, then open Protocols → vMix.
Use “Asignar primeras 24 entradas” or select individual inputs and save.
Positions 1–12 are normal and the next 12 use SHIFT. Assignments are shared by
PST and PGM and stored as GUIDs, not names or mutable input numbers. Two inputs
with the same name remain distinct. Missing/recreated inputs require remapping.
No automatic program or preview changes are sent when saving mappings.

The optional `--vmix` flag chooses vMix at startup, while preserving runtime
selection from web/LCD. It uses the saved vMix endpoint. `--vmix-mappings FILE`
changes the default `vmix-mappings.json` path, independent of OBS's `--mappings`.

Implemented:
- Discovery and state over the official TCP API, initial XML and TALLY events.
- PST: PreviewInput; PGM hot punch: CutDirect (preserves preview).
- CUT: native Cut, with bus swap confirmation.
- AUTO TRANS with MIX: explicit Fade + Duration in milliseconds; TRANS RATE
  controls duration. It does not change vMix's configured transition buttons.
- Confirmed bus feedback and preview HIGH when TALLY reports it on air.
- Reconnection/state resync, bounded frames/timeouts, one outstanding request,
  unsolicited TALLY handling, no automatic replay of an ambiguous take.
- Independent web mappings, revision conflict handling and atomic persistence.

Traditional USK/keyers, extra mixes and output controls are not implemented
in this adapter. Completion/busy tracking is for AUTO launched by picohost;
external bus changes and on-air tally are synchronized, but external transition
progress has no dedicated state event implemented yet. Own AUTO waits for both
its requested duration and confirmation of the target program input.

Linux dependencies add libxml2 development headers and pkg-config. XML replies
are bounded to 4 MiB; DTD documents are rejected and network entity access disabled.

## Authentication

The documented vMix TCP API has no username/password login command. vMix's Web
Controller password is a separate HTTP feature; the TCP client does not pretend
to authenticate using it or weaken vMix security settings.

OBS password is editable under faderOS → OBS authentication. It is stored in
`<config path>.credentials.json` with mode 0600; not encrypted. Responses never
return the password. The saved value overrides BKDS_OBS_PASSWORD; an empty saved
value clears it. Editing credentials reconnects OBS only if OBS is active.
The web server remains loopback-only. The shared `MixerState::auth_required`
flag displays `AUTH REQUIRED - CONFIGURE IN WEB` on the LCD home screen. OBS
sets it for a required missing password or an explicit authentication rejection;
ordinary network failures do not set it. Future authenticated adapters can use
the same flag and web-only credential approach.

## Validation (2026-09-29)

`make test-vmix test-web test-obs test-link test-adapter-router` passed.
The vMix mock fragments frames, interleaves TALLY events, delays bus changes after
FUNCTION OK, uses duplicate input names, and drops the connection after executing
CUT to check that reconnect does not replay it. Web tests verify independent
mappings, revision rejection, credential permissions and no password disclosure.

A direct adapter probe against vMix 29.0.0.49 at 192.168.122.202:8099 passed PST,
PGM hot punch, CUT swap and a 350 ms Fade. Initial testing exposed delayed XML
confirmation and the client was corrected to await actual state. The successful
probe restored the buses it captured at its start. Inputs changed during the
session (Blank/Blank to Colour Bars/Blank), illustrating why GUID mappings must
be refreshed when inputs are recreated. Serial panel + vMix still needs the
operator's end-to-end test; no firmware was flashed.

References:
- https://www.vmix.com/help29/TCPAPI.html
- https://www.vmix.com/help29/ShortcutFunctionReference.html (CutDirect, PreviewInput)
- https://www.vmix.com/help29/WebController.html (HTTP security settings)

## Transition mappings (faderOS 0.12.1)

Selecting MIX in Transition Type opens keypad types 1 Fade, 2 AlphaFade and 3 CrossZoom.
Selecting DME opens keypad types 0 Merge, 1 Zoom, 2 Slide,
3 Fly, 4 FlyRotate, 5 Cube, 6 CubeZoom and 7 VerticalSlide. DME REV uses
SlideReverse and VerticalSlideReverse where assigned; other reverse bindings
start empty. Empty bindings are rejected with a short beep, never substituted.
The LCD displays the selected native effect. Keypad preparation never arms a
new Transition Type. Durations are TRANS RATE milliseconds, 50–20000.

WIPE presets 7 (Sony 1) and 8 (Sony 3) use Wipe and VerticalWipe, with their
native Reverse variants. Presets 9 (Sony 17) and 4 (Sony 18) use BarnDoor and
RollerDoor. Other Sony codes start unassigned. The web allows up to 32 codes,
with NORM and REV assigned separately. DIRECT accepts codes with a normal binding.
SOFT and other geometric modifiers remain pending.

WIPE USER WIPE selects Stinger1–8 on keys 1–8. Configure the animation and cut
point in vMix. TRANS RATE is not sent for stingers. Each web assignment also
stores total animation length in milliseconds (50–60000, default 1000) for local
completion/tally tracking: set it to the actual length configured in vMix. This
metadata does not change the animation. Completion additionally requires target
program confirmation; the API XML does not expose native animation completion.
The eight commands are supported in software; configured physical stingers still
need an operator test. Keys 0 and 9 are unavailable in this mode.

Profile `version` remains 1 for source compatibility. Its `transitions` object
uses version 3, containing three `mix_types`, eight `dme_types` NORM/REV bindings,
`wipes` by Sony code and eight `stingers` (`function`, `total_ms`). Source-only
profiles receive the defaults above. Legacy transition profiles migrate on load:
custom source and wipe bindings are retained, MIX/Slide/Fly overrides remain,
and the former default Merge DME becomes Zoom. Version-2 profiles move MIX 2
to DME 0 and compact MIX 3/4 to 2/3, retaining all other bindings. Saving writes the normalized
schema atomically. Invalid effect names, slot counts or lengths are rejected.

FTB calls native FadeToBlack and reads `fadeToBlack` from XML. External changes
update tally. Its API has no duration argument; configure FTB timing in vMix.
Overlay delegation is described below; traditional USK/keyer integration remains pending.

Merge animates matching inputs between preview and output; unmatched layers fade.
See the [official Merge description](https://www.vmix.com/help29/MergeEffect.html),
[function reference](https://www.vmix.com/help29/ShortcutFunctionReference.html),
and [stinger configuration](https://www.vmix.com/help29/StingerTransitions.html).


## Manual transitions (faderOS 0.13.0)

The calibrated T-bar drives native SetFader (0–255), with the armed MIX, WIPE or
DME effect. Stingers are automatic only. Both physical strokes use the same
normalized range. Only the calibrated endpoint window sends 255; intermediate positions
remain 1–254. Preview tally is HIGH during the mix; AUTO TRANS stays OFF.

vMix uses transition button 1 for the manual fader. faderOS temporarily assigns
that button and restores its previous effect after completion or cancellation,
unless the operator changed it while the transition was active. Pending positions
are coalesced, while a terminal position is latched until confirmed. Cancellation
returns the fader to zero. A lost connection never replays a terminal take.

Keypad options use LOW for selectable, HIGH for selected and OFF for unassigned.
Unassigned presets/bindings are rejected with a short beep. MIX uses keys 1–3;
DME uses 0–7, including Merge on 0. Capability forwarding through the adapter
router is covered by regression tests.

The documented vMix API does not expose a transition-preview command. Double
click on Transition Type therefore gives an unavailable beep; it never executes
a transition on program. ATEM's supported transition-preview mode is unchanged.

References: [manual Fade Bar](https://www.vmix.com/help29/FadeBar.html) and
[SetFader / SetTransitionEffect1](https://www.vmix.com/help29/ShortcutFunctionReference.html).

Validation: the 21-test CMake suite and strict Make build passed (the keypad
expectations were updated from OFF to selectable LOW). The vMix mock checks two
completed manual takes, rollback, explicit cancellation, endpoint protection,
position coalescing and restoration of transition button 1. Physical Sony lever
operation against vMix remains an operator validation; no panel firmware change
is required for this release.

## T-bar effect confirmation (faderOS 0.13.1)

Manual start now reads the live transition-button preference and waits for XML
to confirm the requested effect before sending SetFader. Restoration is likewise
confirmed before the adapter becomes idle. Positions received during preparation
remain coalesced to the latest value. This addresses a possible race between
command acknowledgement and effect application; it does not add a fixed sleep.

A delayed-effect mock reproduces the premature SetFader call in 0.13.0 and passes
with this change, including all four effect restorations. Physical validation of
the intermittent fallback reported by the operator remains pending.

See [vMix macro research](docs/vmix-macros.md) for scripting support and the limits
of emulating traditional panel functions with command sequences.


## Endpoint tolerance (faderOS 0.13.2)

Panel transition control snaps calibrated positions 0–82 to zero and 4013–4095
to full travel (approximately 2% at each end). It keeps intermediate values
unchanged. After endpoint acquisition/completion, departure must exceed 123
counts (approximately 3%) before starting the next stroke. This hysteresis
prevents end-stop jitter from immediately starting another transition. These
thresholds apply to the common panel controller, not just vMix, and leave the
saved analog calibration unchanged.

Tests cover both early endpoints, boundary values, completion latching, rollback
and departure hysteresis. Physical endpoint feel still needs operator validation.

## Overlay delegation (faderOS 0.14.0)

Hold KEY bus position 1–8, then press a source on PST or PGM. SHIFT selects the
upper source bank as usual. PST calls native PreviewOverlayInputN; PGM calls
OverlayInputNIn. Repeating the same source in the same destination removes it:
native preview toggle for PST, OverlayInputNOut for PGM. Choosing another source
replaces it. Without a held KEY layer, the source buses retain their normal use.
Ambiguous layer/source chords are rejected and never fall through to a background
take. Basic HD exposes one channel; other editions use up to eight reported
primary channels.

This deliberately follows vMix's single assignment per overlay channel. Moving
an occupied program channel to preview can remove its program assignment. A
prepared overlay goes on air on the next background cut/transition. faderOS does
not invent separate program/preview assignments or a NEXT TRANS keyer engine.
Configure each channel's geometry and entry/exit effect in vMix; background
TRANS RATE/type does not alter those effects.

The live XML reports overlay source numbers and `preview="True"`; assignments
are resolved through the same GUID input mappings as the main buses. Feedback
uses confirmed XML state, including changes made in vMix and reconnection.
Queued commands are not replayed after a lost reply. Native effect acknowledgement
alone is not treated as confirmation of an overlay assignment.

The Sony KY-307 shares one LED color per twelve-button row. Consequently the
available KEY positions stay LOW, with the held position blinking. Prepared
overlay sources appear on PST; on-air overlay sources appear on PGM, using each
row's existing tally color. There is no simultaneous orange preparation lamp in
the red PGM row. Upper-bank sources retain blinking feedback, independent of SHIFT.

USER WIPE is OFF and ignored in vMix MIX selection (1 Fade, 2 AlphaFade,
3 CrossZoom). The MIX menu's USER WIPE/DIP function belongs to ATEM. vMix's
WIPE USER WIPE stinger selection remains available.

Validation covers all eight channels, add/remove/replace, native PGM-to-PST move,
PST-to-PGM promotion, promotion by CUT, GUID mappings, unchanged background buses,
held-key routing, SHIFT, rejected ambiguous chords and lost-reply reconnection.
The live mixer was inspected for its edition, channel XML and native preview flag;
complete physical-panel overlay operation still needs the operator's test.

References: [input overlay buttons](https://www.vmix.com/help29/Overlay.html),
[per-channel overlay settings](https://www.vmix.com/help29/Overlay2.html), and
[API functions](https://www.vmix.com/help29/ShortcutFunctionReference.html).

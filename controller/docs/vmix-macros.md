# vMix macros and traditional panel operations

Research notes, 2026-10-01. Macro execution is not yet implemented in faderOS.

## Native scripting

vMix provides named scripts in Settings → Scripting, with global or preset-local
scope. Its documentation limits scripting to the 4K and Pro editions; ordinary
shortcut/API functions should not be confused with access to scripting.

- **Web Scripting** chains API-style function lines and has a `Sleep` instruction
  in milliseconds. It suits fixed sequences such as showing a title, waiting,
  then removing it.
- **VB.NET Scripting** adds conditional logic and access to input/overlay state.
  The documented objects include program/preview inputs, input lookup by name,
  number or key, and overlay In/Out/Off operations. It is preferable when the
  action depends on what is actually on air.

The function reference exposes `ScriptStart` and `ScriptStop` with a script name,
as well as `ScriptStopAll`. `ScriptStartDynamic` accepts code, and
`ScriptStopDynamic` stops that dynamic script. These are callable shortcut
functions; faderOS already uses the TCP API's FUNCTION transport for mixer
commands. Named scripts are the clearest first integration target: operators
can inspect and edit their behavior in vMix before assigning a panel control.

Sources: [Scripting and Automation](https://www.vmix.com/help29/ScriptingandAutomation.html),
[Web Scripting](https://www.vmix.com/help29/WebScripting.html),
[VB.NET Scripting](https://www.vmix.com/help29/VBNetScripting.html), and the
[function reference](https://www.vmix.com/help29/ShortcutFunctionReference.html).

## What this can and cannot provide

The reference offers explicit overlay In/Out and immediate Off functions, as
well as a toggle. For reproducible macros, prefer explicit desired states:
retrying a toggle can undo the first successful operation.

A scripted overlay change plus a background transition is a sequence of commands.
The cited documentation does not establish a frame-atomic NEXT TRANS operation
or an overlay bound to manual T-bar progress. We must not present a sequence as
such a hardware capability. Timed overlay automation is a useful separate feature;
key transitions tied to the T-bar need their own validated design.

Sequences may also be scheduled by faderOS using ordinary API commands. That
would avoid depending on native scripting editions for simple operations, but
requires an asynchronous executor, cancellation rules and state confirmation.
It should not block panel traffic or replay ambiguous on-air actions after a
connection failure.

## Proposed integration

1. Independent macro assignments per server profile, configured on the web.
2. Named-script actions first, with an explicit indication of edition requirements.
3. Explicit sequences for common operations where native scripting is unnecessary.
4. Feedback based on resulting mixer state, not just acceptance of ScriptStart.
5. Define cancellation and concurrent-execution behavior before exposing controls
   that can change program. Stopping a script must not be advertised as restoring
   the previous mixer state without a tested rollback implementation.

No scripts were installed in the live mixer during this research.

# kavtor SuperSource delegation

Requires kavtor 0.11.0 and its native SuperSource input support. No BKDS firmware
update is needed: this adds host control logic using existing buttons and LEDs.

The two previously empty buttons beside KEY1/KEY2/DSK delegation are:

* Left / button 104: the SuperSource in the active M/E's preview bus.
* Right / button 105: the SuperSource in its program bus.

Choose a delegation, choose a mapped window on KEY, then hot-punch its source
on AUX. SHIFT accesses the second window/source bank. Available crosspoints use
LOW, and the selected window and source blink in LOW. KY-307 shares one color
selector per bus, so LOW and HIGH cannot coexist within either row. Selected
crosspoints remain visible when changing SHIFT banks.
The last AUX button selects the next M/E's flattened program, independent of
SHIFT. The binding retains that explicit M/E after changing panel delegation.
It is unavailable on M/E 4; kavtor rejects direct or indirect feedback routes.
The LCD identifies PREVIEW or PROGRAM, the input instance and the chosen window.
Re-pressing the same delegation exits it; normal KEY/DSK, frame-memory or AUX
output delegation also restores the rows' previous purpose.

A take keeps the delegation on the explicitly selected bus. Preview delegation
never changes itself to program. The KEY position remains selected and resolves
against the new SuperSource's window map. An absent mapping beeps; no different
window is selected automatically. Editing program requires the right delegation
button. Window punches are always cuts, even during a running background take.
Use an M/E input for other effects inside a window.

The window map and default inputs are prepared in kavtor's SuperSource editor.
Each mapped input box has a unique KEY crosspoint. Static image elements cannot
be rerouted. Runtime changes do not alter the saved default bindings.

Panel requests carry the delegated bus, M/E, input instance, box ID and KEY
position. kavtor rejects stale context rather than editing the instance that has
moved to program. Metadata is refreshed with tally changes. Rejected window
changes beep without cancelling a background take.

A SuperSource input remains one shared composition wherever that input is used.
For independent preview/program content, assign separate input instances; these
may share a layout design. There is no implicit private preview clone.

Tests cover adapter-router forwarding, delegation, bus swapping, retained window position, explicit program
selection, upper-bank feedback and absence of unintended main-bus requests.
Mock-server tests verify the request context and transition-preserving errors.

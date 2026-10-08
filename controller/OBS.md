# OBS adapter

Requires OBS WebSocket 5 / RPC 1 (default port 4455), Studio Mode and libcurl
WebSocket support. Select OBS with `--obs`, SYSTEM SETUP or the web server page.
Configure scenes/transitions in the OBS web tabs. `--mappings FILE` selects the
mapping file. Empty transition mappings permit automatic type discovery only
when exactly one candidate exists.

PST selects preview; PGM performs hot punch. Disable OBS Studio Mode's
“Swap Preview/Program Scenes After Transitioning” option; the panel implements
its own CUT swap. During a transition preview tally becomes HIGH.

MIX defaults to Fade; WIPE uses Luma Wipe or a USER WIPE stinger; DME can map to
Move, Slide and Swipe. Named transitions and multiple stingers are configurable
on the web. SOFT supports global and per-pattern custom values. Double-click
resets softness. Pending keypad edits are committed only with ENTER.

The optional Exeldro Downstream Keyer plugin supports two assigned DSK channels.
Add each mapped scene to its channel with TIE disabled. DSK ON cuts; DSK MIX
fades; hold or double-click DSK PVW to operate the second channel.
ENABLE EDITOR controls streaming, ENABLE DME recording, and ENABLE GPI virtual
camera. FTB state synchronization remains pending. Manual T-bar completion is
known to be unreliable with the tested OBS version and is not production-ready.

Passwords are configured on the web and stored in local per-profile credentials
files, never displayed on the LCD. Authentication failures direct the operator
to web configuration. Keep credential files out of Git.

Local peer tests cover connection/authentication/reconnect, buses, transitions,
DSK and outputs. Actual plugin/version behavior still needs physical acceptance.

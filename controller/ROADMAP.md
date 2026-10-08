# Agreed next steps and review notes

## Local review, 0.8.0

Branch: `codex/picohost-consistency-review`. No panel firmware change is needed.
Restart the host after rebuilding. Physical checks to run on return:

- USER MENU 1–3: independent endpoints/maps, correct bus tallies after switching,
  external changes retained on inactive servers, no duplicated CUT/AUTO.
- Profile selection during a transition waits; changing profile cancels the old
  keypad edit. Check the slot shown in SERVER/INFO and the web header.
- Web: changes in one profile never alter another; a previous browser tab asks for
  reload and cannot send commands. Unsaved key/DSK forms warn before reload.
- HDMI sources and USB PROGRAM/PREVIEW/MULTIVIEW, from LCD and web Salidas.
- Multiview SAFE/meters individually and all inputs; PROGRAM/PREVIEW stay separate.
- Equipo video-format confirmation and SERVER SETUP INFO navigation.

## MIDI over the network

Approved as an additional backend, including audio/DAW control. Start with
RTP-MIDI/AppleMIDI interoperability, configurable button Note/CC events and
incoming Note/CC LED feedback. Encoders/analogs need explicit range/channel
mapping. A MIDI endpoint does not inherently expose mixer state or LED semantics;
feedback mappings must be defined by the receiving software. Avoid assuming that
all applications return feedback. Keep real mixer APIs for native video control.
No MIDI runtime or web configuration has been implemented in 0.8.0.

## RP2350 hardware — first version

First Stream Deck test target: 15-key Elgato 20GAA9902. Larger models require
individual USB identification, packet/image-format and bandwidth tests. A desktop
HID driver does not prove compatibility with the embedded USB host.

The agreed first target is RP2350 plus W5500 and an additional USB host port.
ESP32/Wi-Fi/BLE exploration is deferred to a possible second version.
Native USB device plus PIO USB host is a candidate architecture; an external
host controller is an alternative. Validate under concurrent serial, Ethernet
and three mixer sessions before committing to the hardware. The RP2350/W5500 port
and its socket/memory budget remain pending. A Pico SDK UART/USB CDC bring-up
application now builds; it has not been validated on physical Pico hardware.

Sony supply headroom is unknown: measure its 5 V rail under maximum panel LED
load, then add the Pico/Ethernet/interface and Stream Deck load. Provide a
current-limited USB host supply and isolate PC/console power paths to avoid
backfeeding. A powered hub is an option if the internal supply lacks margin.
Do not assign a current consumption to the Stream Deck without measurement or
verified model documentation.

References already researched:
- https://github.com/sekigon-gonnoc/Pico-PIO-USB
- https://github.com/abcminiuser/python-elgato-streamdeck
- https://github.com/lathoub/Arduino-AppleMIDI-Library
- https://github.com/davidmoreno/rtpmidid

## Project names

The panel control platform and its panel firmware implementations are named
**faderOS**, with this exact spelling. BKDS-2010 is the first supported panel.
Keep compatibility paths and binary protocol identifiers during the migration.
The separate mixer currently called kavtor still awaits its final name.

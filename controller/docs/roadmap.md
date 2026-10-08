# Open work — faderOS

Reviewed against host 0.26.0 on 2026-10-07. Sony panel scanning, LEDs, LCD/CGRAM,
analog calibration/streaming, 38400 operation and remote flash/reboot already
work; they are not new implementation tasks.

## Current integration

- Extend remaining kavtor key processing beyond the implemented LINEAR/LUMA/CHR,
  MAIN MASK and KEY INV; unsupported keys retain rejection feedback.
- Physically validate the new LUMA selection and per-key/DSK KEY INV feedback.
- Validate new Sony DME selections and per-ID preparation on the physical panel.
- Integrate operator-supplied 20x16 Sony pictograms in transition selection.
  Serial CGRAM transfer and large direction arrows already work.
- Finish physical validation of every SYSTEM SETUP TEST diagnostic page.

## RP2350 / W5500 port

The embedded target currently provides a UART bring-up probe, not the complete
controller. Linux remains the full development/runtime target.

- Select exact board/pins and validate MAX3232 UART at 9600 then 38400.
- Port one ATEM session/basic buses, then remaining mixer adapters.
- Add Ethernet/DHCP/DNS, persistent settings, web management and update client.
- Implement NTP/timezone configuration; Linux currently supplies its local clock.
- Validate the two-core panel/external-I/O architecture and nonblocking queues.
- Measure heap/RAM/flash fit and simultaneous three-profile/socket budgets.
- Add additional USB host/Stream Deck support and USB-device maintenance path.
- Validate power budget, host current limit and isolation from USB-device VBUS.

## Other integrations and decisions

- Implement network MIDI event/LED mapping, including mapping preparation.
- Add vMix named-script/macros and optional asynchronous sequences; do not claim
  sequences provide hardware-atomic NEXT TRANS or restore state when cancelled.
- OBS FTB with reliable reported state and OBS manual T-bar completion remain
  deferred. Do not re-enable controls merely because a command can be sent.
- Decide a safe/ergonomic DSK TIE control; delegation buttons remain reserved.
- Study monitor/debug entry from our firmware as a recovery aid. A jump cannot
  recover code that no longer boots. EPROM/DIP recovery remains independent.
- Review remaining emulator fidelity if needed; the COMM-test anomaly was
  explicitly deferred and is not a blocker for the real-panel implementation.

## Publication

- Choose license and review attribution/public assets.
- Validate clean standalone source bundles and exclude parent workspace/history,
  credentials, captures, proprietary ROMs/manuals and historical prototypes.
- Create GitHub repository and require CI/reviews; then issue → PR → merge.
- Keep software/docs/comments primarily English and exact spelling faderOS.

## Outside the first hardware version

76800 remains deferred; 38400 is the validated operating rate. ESP32/Wi-Fi/BLE,
FPGA replacement and an additional OLED are possible later experiments, not
requirements for the RP2350/W5500 controller.

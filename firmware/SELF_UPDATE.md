# RAM self-update and remote boot

The updater is validated on the physical panel, including erase/program, CRC
readback and remote restart. It runs independently in RAM; it does not execute
from the flash being modified. The original Sony EPROM remains the recovery
path if application/resident flash becomes invalid.

Close faderOS and other serial owners. The updater uses **9600 baud**. Its
handshake identifies both AM29F040 devices (manufacturer 0101/device A4A4).

```sh
python3 firmware/tools/self_update.py --port /dev/ttyUSB0 --probe
python3 firmware/tools/self_update.py --port /dev/ttyUSB0 \
  --image firmware/build/flash-link.bin --stage-only
python3 firmware/tools/self_update.py --port /dev/ttyUSB0 \
  --image firmware/build/flash-link.bin
```

`--probe` checks capability without staging/erasing/programming, though opening
a new link session may reset host-controlled outputs. `--stage-only` transfers
and CRC-checks application sector 1 in RAM without COMMIT; it remains in the
RAM updater. `--enter` enters that updater without transferring an image.

Normal update receives complete sectors in RAM, validates CRC, programs only
changed sectors and verifies flash readback. Application sectors are updated
first; the complete application is checked before resident sector 0 is changed.
EPROM and reserved sector 7 are never written. An identical image skips writes
and therefore does not validate erase/program behavior.

Successful updates request remote boot automatically. `--no-boot` leaves the
panel in the updater. A new invocation can resume from the updater; preserve
its transcript and use the documented recovery path after interrupted writes.
LCD progress is informational. Reconnect faderOS at the normal startup rate and
allow negotiation to 38400; confirm firmware version and physical controls.

Local checks: `make -C firmware test-self-update` validates update logic and the
serial client with fixtures. Physical acceptance remains a separate check.

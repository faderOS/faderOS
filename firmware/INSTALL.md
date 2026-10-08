# First installation and recovery through the Sony monitor

The monitor is retained for first installation and independent recovery.
Subsequent updates use the [RAM updater](SELF_UPDATE.md). These procedures were
validated on a physical BKDS-2010; emulator success is not equivalent evidence.

Use SCI2/RS232 at **9600 8N1** with the correct electrical interface. Close the
controller and terminals using the port. Cooperative port locking cannot stop
other programs that ignore the lock.

Before power-on select **DS0 Debug ON and DS1 Monitor interface ON**. DS1 ON
selects SCI2; OFF selects parallel download. Wait for the Sony monitor prompt
with no pending command/question. Do not change DIP switches or reset during
a write. The original EPROM must remain intact.

```sh
make -C firmware protocol
python3 firmware/tools/install.py prepare --port /dev/ttyUSB0 \
  --image firmware/build/flash-link.bin --bundle /path/to/new-backup
python3 firmware/tools/install.py inspect --bundle /path/to/new-backup
python3 firmware/tools/install.py install --port /dev/ttyUSB0 \
  --bundle /path/to/new-backup
```

`prepare` reads without erasing/programming. Its bundle contains the complete
EPROM and complete sectors affected by that candidate, not necessarily every
flash sector. Keep it outside this repository. Installation performs preflight,
programming and verification, application before resident. It never targets the
EPROM or sector 7. Do not use a full-chip erase command.

`install --fast` is for an already verified backup/candidate workflow: it skips
slow host hex readbacks but retains Sony IL flash verification. It is not the
first-installation procedure. Read `install.py --help` for inspect, install and
restore options. A failed serial transfer does not establish completion; keep
the monitor available and verify state before normal boot.

After complete verification, return to normal boot and connect faderOS. Later
updates can enter RAM remotely without reopening the panel for DIP changes.

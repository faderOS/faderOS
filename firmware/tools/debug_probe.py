#!/usr/bin/env python3
"""Read-only Sony monitor probe. Does not program flash."""
import re
import select
import sys
import time
from pathlib import Path
from sony_monitor import Monitor, MonitorError

PROMPT = re.compile(rb"(?:^|[\r\n])\x07?[A-Z]*>$")
PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyUSB0"
LOG = Path(__file__).resolve().parents[1] / "build" / f"debug-probe-{time.time_ns()}.serial.log"
LOG.parent.mkdir(exist_ok=True)


class DebugMonitor(Monitor):
    def wait(self, pattern=PROMPT, timeout=30):
        return Monitor.wait(self, pattern, timeout)


def show(title, data):
    print(f"\n=== {title} ===")
    sys.stdout.buffer.write(data.replace(b"\r\n", b"\n").replace(b"\r", b"\n"))
    if not data.endswith(b"\n"):
        print()
    sys.stdout.flush()


def hexdump(label, addr, blob):
    print(f"\n=== {label} @ {addr:06x} ({len(blob)} bytes) ===")
    for i in range(0, len(blob), 16):
        chunk = blob[i : i + 16]
        hext = " ".join(f"{b:02x}" for b in chunk)
        ascii_ = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        print(f"{addr + i:08x}  {hext:<47}  {ascii_}")
    sys.stdout.flush()


def listen(m, seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        remaining = end - time.monotonic()
        if select.select([m.fd], [], [], max(0.0, remaining))[0]:
            m._read()
    return bytes(m.pending)


def main():
    print(f"opening {PORT}, log {LOG}", flush=True)
    m = DebugMonitor(PORT, LOG)
    try:
        m.sync()
        print("SYNC ok", flush=True)
        hexdump("header", 0x60000, m.read(0x60000, 0x20))
        hexdump("TCR/timers", 0xFFFE00, m.read(0xFFFE00, 0x50))
        hexdump("IMR/ICR", 0xFFFC80, m.read(0xFFFC80, 0x20))
        hexdump("D00000", 0xD00000, m.read(0xD00000, 0x10))
        show("DI firmware_main 61ca0", m.command("di 61ca0 61d10", check_errors=False))
        hay = m.read(0x61C00, 0x400)
        idx = hay.find(b"BKDS")
        print(f"\nBKDS index in 61c00: {idx}")
        if idx >= 0:
            hexdump("BKDS", 0x61C00 + idx, hay[idx : idx + 64])
        else:
            hexdump("61c00", 0x61C00, hay)
        print("\n=== G 60020, listen 6s ===", flush=True)
        m.pending.clear()
        t0 = time.monotonic()
        m.send(b"g 60020\r")
        out = listen(m, 6.0)
        print(f"elapsed {time.monotonic() - t0:.2f}s, {len(out)} bytes")
        show("G output", out)
    finally:
        m.close()
        print(f"\nclosed. transcript {LOG}", flush=True)


if __name__ == "__main__":
    try:
        main()
    except MonitorError as e:
        print(f"MONITOR ERROR: {e}", file=sys.stderr)
        sys.exit(1)

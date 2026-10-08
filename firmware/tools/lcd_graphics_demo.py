#!/usr/bin/env python3
"""Show custom LCD glyphs over the panel link; no flash writes or mixer actions."""
import argparse
from pathlib import Path
import sys
import termios
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'controller/python'))
from protocol import Client

ARROWS = (
    (4, 14, 21, 4, 4, 4, 4, 0), (4, 4, 4, 4, 21, 14, 4, 0),
    (0, 4, 8, 31, 8, 4, 0, 0), (0, 4, 2, 31, 2, 4, 0, 0),
    (31, 24, 20, 18, 1, 0, 0, 0), (31, 3, 5, 9, 16, 0, 0, 0),
    (0, 0, 1, 18, 20, 24, 31, 0), (0, 0, 16, 9, 5, 3, 31, 0),
)


def picture(mode, direction=8):
    cells = bytearray(b' ' * 80)
    title = b'faderOS CGRAM / EIGHT ARROWS' if mode == 'arrows' else b'faderOS TILED ARROW / 15x16'
    cells[:len(title)] = title
    if mode == 'arrows':
        glyphs = [bytes(rows) for rows in ARROWS]
        for slot in range(8):
            cells[40 + slot * 4] = slot
    else:
        glyphs = [bytearray(8) for _ in range(6)]

        dx, dy = {7: (-1, -1), 8: (0, -1), 9: (1, -1),
                  4: (-1, 0), 6: (1, 0), 1: (-1, 1),
                  2: (0, 1), 3: (1, 1)}[direction]
        for y in range(16):
            for x in range(15):
                xx, yy = x * 2 - 14, y * 2 - 15
                along, across = xx * dx + yy * dy, -xx * dy + yy * dx
                if dx and dy:
                    along, across = int(along * 7 / 10), int(across * 7 / 10)
                across = abs(across)
                shaft = -13 <= along <= 2 and across <= 3
                head = 0 <= along <= 14 and across <= 14 - along
                if shaft or head:
                    glyphs[(y // 8) * 3 + x // 5][y % 8] |= 1 << (4 - x % 5)
        for row in range(2):
            for col in range(3):
                cells[row * 40 + 36 + col] = row * 3 + col
    return glyphs, bytes(cells)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--mode', choices=('arrows', 'bitmap'), default='arrows')
    parser.add_argument('--direction', type=int, choices=(1, 2, 3, 4, 6, 7, 8, 9), default=8,
                        help='Bitmap arrow direction, using the panel keypad compass')
    parser.add_argument('--seconds', type=float, help='Hold for this duration instead of until Ctrl-C')
    args = parser.parse_args()
    if args.seconds is not None and args.seconds <= 0:
        parser.error('--seconds must be positive')
    client = Client(args.port)
    try:
        print('9600 baud / waiting 5.5 s for the previous session to expire...', flush=True)
        time.sleep(5.5)
        termios.tcflush(client.fd, termios.TCIFLUSH)
        client.connect()
        if not client.capabilities & 0x200:
            raise RuntimeError('LCD graphics unsupported; update panel firmware to 0.23 first')
        glyphs, cells = picture(args.mode, args.direction)
        started = time.monotonic()
        if args.mode == 'arrows' and client.capabilities & 0x400:
            status = client.command(0x18, bytes(range(9)))
            print('Using panel ROM icons (8 IDs instead of 64 row bytes).')
        else:
            status = client.command(0x16, b'\x00' + b''.join(bytes(rows) for rows in glyphs))
        if status:
            raise RuntimeError('Glyph batch rejected')
        if client.command(0x17, b'\x00' + cells):
            raise RuntimeError('LCD cells rejected')
        print(f'Upload accepted in {(time.monotonic() - started) * 1000:.1f} ms (ACK, not display completion).')
        print(f'{args.mode} queued; verify the physical LCD. Ctrl-C to finish.', flush=True)
        end = time.monotonic() + args.seconds if args.seconds else float('inf')
        while time.monotonic() < end:
            client.poll(.01)
            client.heartbeat()
    finally:
        client.close()


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print('Closed; the panel watchdog will restore its waiting screen.')
    except (OSError, RuntimeError, ValueError, TimeoutError) as error:
        print(f'STOPPED: {error}', file=sys.stderr)
        sys.exit(1)

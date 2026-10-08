"""Build a prototype flash image. Not a Sony IL transport file."""
from pathlib import Path
import struct
import zlib

def pack(boot, app):
    if not 2 <= len(boot) <= 0x20000:
        raise ValueError('resident exceeds sector 0')
    app += b'\xff' * (len(app) % 2)
    if not 2 <= len(app) <= 0xc0000 - 32:
        raise ValueError('application exceeds sectors 1..6')
    header = struct.pack('>8I', 0x424b4131, 1, len(app), 0x60020,
                         zlib.crc32(app), 0, 0, 0)
    flash = bytearray(b'\xff' * 0x100000)
    flash[:len(boot)] = boot
    flash[0x20000:0x20000+len(header)+len(app)] = header+app
    return flash

if __name__ == '__main__':
    import argparse
    parser=argparse.ArgumentParser()
    parser.add_argument('--app', type=Path)
    parser.add_argument('--output', type=Path)
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[1]/'build'
    flash=pack((root/'boot.bin').read_bytes(), (args.app or root/'app.bin').read_bytes())
    (args.output or root/'flash.bin').write_bytes(flash)
    print('Built prototype flash: resident sector 0; application sectors 1..6; sector 7 reserved')

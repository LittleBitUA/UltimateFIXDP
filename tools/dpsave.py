#!/usr/bin/env python3
"""Reads dp.sav of Deadly Premonition: The Director's Cut (PC) and lists its records. Read-only.

Layout (verified on a real save, 8 005 152 bytes = 0x120 + 28 x 0x45CC0):
  header 0x120 bytes: +0x20 float money, +0x24..+0x38 six u32 = year, month, day, hour, minute, second of the save
  record i at 0x120 + i*0x45CC0: record 0 is the current (working) one, records 1..27 are the slots
  inside a record: +0x7C suit digit, +0x7D suit variant, +0x88 float money, +0xA0 chapter (0..8), +0xA1 progress step (0..0x19),
  +0x84 u32 story flags, +0x4AA playable kind (0 York, 4 Zach), +0x42800 u32 play time in seconds
Usage: dpsave.py dp.sav
"""
import struct
import sys

HEADER, RECORD, COUNT = 0x120, 0x45CC0, 28


def main(path):
    d = open(path, 'rb').read()
    print('size', len(d), 'expected', HEADER + COUNT * RECORD)
    money = struct.unpack_from('<f', d, 0x20)[0]
    stamp = struct.unpack_from('<6I', d, 0x24)
    print('header: money %.1f, saved %04d-%02d-%02d %02d:%02d:%02d' % ((money,) + stamp))
    for i in range(COUNT):
        r = d[HEADER + i * RECORD: HEADER + (i + 1) * RECORD]
        secs = struct.unpack_from('<I', r, 0x42800)[0]
        m = struct.unpack_from('<f', r, 0x88)[0]
        print('%2d  chapter %d step %2d  %5.1f h  money %8.1f  suit %d/%d  kind %d  flags %08X  %s' % (
            i, r[0xA0], r[0xA1], secs / 3600.0, m, r[0x7C], r[0x7D], r[0x4AA], struct.unpack_from('<I', r, 0x84)[0],
            'current' if i == 0 else ''))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'dp.sav')

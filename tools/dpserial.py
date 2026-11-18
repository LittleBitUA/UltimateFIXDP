#!/usr/bin/env python3
"""dpserial.py - read-only reader for Deadly Premonition: The Director's Cut (Steam PC)
DPSerial.001/.002/.003 archives. The resource index lives inside DP.exe (VA 0x8D8D78,
18340 records of 0x20 bytes), not inside the archives.

Usage:
  python3 -I dpserial.py --exe DP.exe --arch DIR list [--grep TEXT] [--ext XMD]
  python3 -I dpserial.py --exe DP.exe --arch DIR info <id|NAME>
  python3 -I dpserial.py --exe DP.exe --arch DIR extract <id|NAME> [-o OUT] [--raw]
  python3 -I dpserial.py --exe DP.exe --arch DIR extract-all OUTDIR [--ext XPC]

DIR contains DPSerial.001/.002/.003 (case-insensitive). NAME is matched (exact, then UPDATA/+NAME, then suffix) as a case-insensitive
suffix of the full 'UPDATA/...' path (must be unique) or a full path.
Record layout (verified, see docs/engine/02_resources_formats.md):
  +00 u32 offset   +04 u32 comp (XZP1 payload size, 0 = not compressed)
  +08 u32 size     +0C u32 archive (0..2)  +10 u32 aux  +14 i32 lang group
  +18 u32 id       +1C u32 VA of name string
License: MIT (own code, based on facts verified against the user's own files).
"""
import argparse, os, struct, sys, zlib

INDEX_VA, INDEX_N = 0x8D8D78, 18340
LANGS = ["JP", "EN", "FR", "GE", "IT", "SP", "US"]


class Exe:
    def __init__(self, path):
        self.d = open(path, "rb").read()
        pe = struct.unpack_from("<I", self.d, 0x3C)[0]
        nsec = struct.unpack_from("<H", self.d, pe + 6)[0]
        opt = struct.unpack_from("<H", self.d, pe + 20)[0]
        base = struct.unpack_from("<I", self.d, pe + 24 + 28)[0]
        self.secs = []
        off = pe + 24 + opt
        for i in range(nsec):
            vs, va, rs, ro = struct.unpack_from("<IIII", self.d, off + 8 + i * 40)
            self.secs.append((base + va, max(vs, rs), ro, rs))
        self.base = base

    def va2off(self, va):
        for s, l, ro, rs in self.secs:
            if s <= va < s + l:
                return ro + va - s
        raise ValueError("VA %#x not mapped" % va)

    def cstr(self, va):
        o = self.va2off(va)
        e = self.d.index(b"\0", o)
        return self.d[o:e].decode("latin1")


class Rec:
    __slots__ = ("idx", "off", "comp", "size", "arch", "aux", "grp", "id", "name")


def load_index(exe):
    o = exe.va2off(INDEX_VA)
    out = []
    for i in range(INDEX_N):
        f = struct.unpack_from("<IIIIIiII", exe.d, o + i * 32)
        r = Rec()
        r.idx = i
        r.off, r.comp, r.size, r.arch, r.aux, r.grp, r.id, nva = f
        r.name = exe.cstr(nva) if nva else ""
        out.append(r)
    return out


def is_missing(r):
    return r.off == 0xFFFFFFFF and r.size == 0xFFFFFFFF


def is_dir(r):
    return r.off == 0 and r.size == 0


def find(recs, key):
    if key.lower().startswith("0x"):
        return recs[int(key, 16)]
    if key.isdigit():
        return recs[int(key)]
    k = key.upper().replace("\\", "/")
    hit = ([r for r in recs if r.name.upper() == k] or [r for r in recs if r.name.upper() == "UPDATA/" + k.lstrip("/")]
           or [r for r in recs if r.name.upper().endswith("/" + k.lstrip("/"))])
    if len(hit) != 1:
        raise SystemExit("name %r matches %d records" % (key, len(hit)))
    return hit[0]


def arch_path(adir, n):
    for fn in os.listdir(adir):
        if fn.lower() == "dpserial.%03d" % (n + 1):
            return os.path.join(adir, fn)
    raise SystemExit("DPSerial.%03d not found in %s" % (n + 1, adir))


def read_rec(r, adir, raw=False):
    """Return bytes of the resource as the game sees them (XZP1 unwrapped unless raw)."""
    with open(arch_path(adir, r.arch), "rb") as f:
        f.seek(r.off)
        head = f.read(0x100 + 4 + 16)
        name_in_arch = head[:0x100].split(b"\0")[0].decode("latin1")
        embedded = struct.unpack_from("<I", head, 0x100)[0]
        if head[0x104:0x108] == b"XZP1":  # game: FUN_006B0000 path
            nxt, comp, unc = struct.unpack_from("<III", head, 0x108)
            f.seek(r.off + 0x104)
            blob = f.read(16 + comp)
            if raw:
                return blob
            data = zlib.decompress(blob[16:16 + comp])
            if len(data) != unc:
                raise SystemExit("XZP1 size mismatch %d != %d" % (len(data), unc))
            return data
        f.seek(r.off + 0x104)
        return f.read(r.size)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--arch", required=True, help="dir with DPSerial.00x")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("list"); p.add_argument("--grep"); p.add_argument("--ext")
    p = sub.add_parser("info"); p.add_argument("key")
    p = sub.add_parser("extract"); p.add_argument("key"); p.add_argument("-o"); p.add_argument("--raw", action="store_true")
    p = sub.add_parser("extract-all"); p.add_argument("outdir"); p.add_argument("--ext")
    a = ap.parse_args()
    recs = load_index(Exe(a.exe))
    if a.cmd == "list":
        for r in recs:
            if a.grep and a.grep.upper() not in r.name.upper():
                continue
            if a.ext and not r.name.upper().endswith("." + a.ext.upper()):
                continue
            kind = "MISSING" if is_missing(r) else "DIR" if is_dir(r) else "file"
            print("%5d 0x%04X %-7s ar=%d off=0x%08X comp=0x%X size=0x%X grp=%d %s" % (
                r.id, r.id, kind, r.arch, r.off & 0xFFFFFFFF, r.comp, r.size & 0xFFFFFFFF, r.grp, r.name))
    elif a.cmd == "info":
        r = find(recs, a.key)
        print("id=%d (0x%X) name=%s\n arch=%d off=0x%X comp=0x%X size=0x%X aux=0x%X group=%d" % (
            r.id, r.id, r.name, r.arch, r.off, r.comp, r.size, r.aux, r.grp))
        if 0 <= r.grp < 146:
            print(" language group row", r.grp, "(langTable VA 0x9BB748, 7 x int16 per row: " + ",".join(LANGS) + ")")
        if not (is_missing(r) or is_dir(r)):
            d = read_rec(r, a.arch)
            print(" bytes after XZP1 unwrap: %d, head: %s" % (len(d), d[:32].hex(" ")))
    elif a.cmd == "extract":
        r = find(recs, a.key)
        d = read_rec(r, a.arch, a.raw)
        out = a.o or os.path.basename(r.name)
        open(out, "wb").write(d)
        print("wrote", out, len(d))
    elif a.cmd == "extract-all":
        n = 0
        for r in recs:
            if is_missing(r) or is_dir(r) or not r.name:
                continue
            if a.ext and not r.name.upper().endswith("." + a.ext.upper()):
                continue
            p = os.path.join(a.outdir, *r.name.split("/"))
            os.makedirs(os.path.dirname(p), exist_ok=True)
            open(p, "wb").write(read_rec(r, a.arch))
            n += 1
        print("extracted", n)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# Host check of the resource read guard (src/modules/resource_guard.cpp) without the game: the readers of DP.exe 1.01b
# (FUN_006B0000, FUN_006AF370, FUN_006AF5B0 and the game's own zlib 1.2.1) and the mod's compiled installer and workers run
# in the Unicorn x86 emulator, once stock and once patched by the mod's real InstallResourceGuard(), over records of a
# real archive. Only the C runtime under them (operator new/delete, fread, fseek, fclose, zcalloc/zcfree) and the Windows
# calls of the DLL are stood in for by Python. It checks that
#   - every packed record unpacks to the same bytes, size, file reads and allocations with and without the patch,
#     and that the stubs leave every register and the flags as they found them;
#   - a record that does not unpack (damaged, cut short) loops without end in the stock reader and ends after 5 reads
#     in the patched one, with 0 returned, the size set to 0 and every buffer freed;
#   - a stream that holds more than its header declares writes behind the buffer in the stock reader and not in the
#     patched one;
#   - made-up streams at the edges (sizes around the 20000h output room, the trailer behind a 10000h input chunk) go
#     through both readers the same way.
# This is an emulation on the host, not a run of the game.
#
# Needs `pip install unicorn`, DP.exe, one archive (DPSerial.002 holds all packed records but 14), and a build of the DLL
# with symbols:
#   make OUT=/tmp/ufxsym LDFLAGS="-shared -nostdlib -nostartfiles -Wl,-e,_DllEntry@12 -Wl,--kill-at -Wl,--dynamicbase -Wl,--nxcompat" \\
#        /tmp/ufxsym/X3DAudio1_7.dll        (the Makefile's link flags without -s)
#   i686-w64-mingw32-nm /tmp/ufxsym/X3DAudio1_7.dll > /tmp/ufxsym/syms.txt
#   python3 tools/emu_resguard.py DP.exe /tmp/ufxsym/X3DAudio1_7.dll DPSerial.002 /tmp/ufxsym/syms.txt SMALL RANDOM [FROM TO]
# SMALL = how many records under 20000h to compare, RANDOM = how many others (3012 3012 = all of them, about half an
# hour); FROM TO = only that slice of the chosen records and nothing else (to run several processes side by side).
import struct, sys, zlib, random
from unicorn import *
from unicorn.x86_const import *

EXE, DLL, ARC = sys.argv[1], sys.argv[2], sys.argv[3]

HOOKPAGE = 0x44000000
SENTINEL = 0x45000000
STACK = 0x0FE00000
STACK_SIZE = 0x200000
HEAP = 0x20000000
HEAP_SIZE = 0x18000000
ARENA = 0x43000000
OBJ = 0x40000000
TEB = 0x41000000
PAD = 0x100000  # canary behind every allocation

def pe_sections(d):
    nt = struct.unpack_from('<I', d, 0x3C)[0]
    nsec = struct.unpack_from('<H', d, nt + 6)[0]
    optsz = struct.unpack_from('<H', d, nt + 20)[0]
    opt = nt + 24
    base = struct.unpack_from('<I', d, opt + 28)[0]
    size = struct.unpack_from('<I', d, opt + 56)[0]
    secs = []
    o = opt + optsz
    for i in range(nsec):
        vs, va, rs, rp = struct.unpack_from('<4I', d, o + i * 40 + 8)
        secs.append((va, vs, rp, rs))
    imp_rva = struct.unpack_from('<I', d, opt + 96 + 8)[0]
    return base, size, secs, imp_rva

class Machine:
    def __init__(self):
        uc = self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.exe = open(EXE, 'rb').read()
        self.dll = open(DLL, 'rb').read()
        self.map_image(self.exe)
        self.dll_base = self.map_image(self.dll)
        uc.mem_map(HOOKPAGE, 0x10000)
        uc.mem_map(SENTINEL, 0x1000)
        uc.mem_write(SENTINEL, b'\xF4' * 16)
        uc.mem_map(STACK, STACK_SIZE)
        uc.mem_map(HEAP, HEAP_SIZE)
        uc.mem_map(ARENA, 0x10000)
        uc.mem_map(OBJ, 0x10000)
        uc.mem_map(TEB, 0x1000)
        # fs: a data segment whose base is the fake TEB (the loose reader keeps its exception chain at fs:[0])
        GDT = 0x42000000
        uc.mem_map(GDT, 0x1000)
        def entry(base, limit, access, flags):
            return struct.pack('<HHBBBB', limit & 0xFFFF, base & 0xFFFF, (base >> 16) & 0xFF, access, ((limit >> 16) & 0xF) | (flags << 4), (base >> 24) & 0xFF)
        uc.mem_write(GDT + 8 * 1, entry(0, 0xFFFFF, 0x9B, 0xC))    # flat 32-bit code
        uc.mem_write(GDT + 8 * 2, entry(0, 0xFFFFF, 0x93, 0xC))    # flat 32-bit data and stack
        uc.mem_write(GDT + 8 * 3, entry(TEB, 0xFFF, 0x93, 0x4))    # fs
        uc.reg_write(UC_X86_REG_GDTR, (0, GDT, 0xFFF, 0))
        uc.reg_write(UC_X86_REG_SS, 0x10)
        uc.reg_write(UC_X86_REG_CS, 0x08)
        uc.reg_write(UC_X86_REG_DS, 0x10)
        uc.reg_write(UC_X86_REG_ES, 0x10)
        uc.reg_write(UC_X86_REG_FS, 0x18)
        def unmapped(uc, access, address, size, value, user):  # says where, then lets the emulator stop
            print('UNMAPPED access %d at %08X size %d, eip %08X esp %08X' % (access, address, size, uc.reg_read(UC_X86_REG_EIP), uc.reg_read(UC_X86_REG_ESP)))
            return False
        uc.hook_add(UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED | UC_HOOK_MEM_FETCH_UNMAPPED, unmapped)
        self.heap_top = HEAP
        self.allocs = {}      # address -> size (live)
        self.all_allocs = []  # (address, size) ever made
        self.stats = dict(new=0, free=0, sleep=0, inflate_calls=0, fread=[], fseek=[], open=0, close=0)
        self.files = {}       # handle -> [bytes, pos]
        self.next_handle = 0x50000000
        self.imports = {}
        self.fill_imports()
        self.game_hooks()
        self.trace = {}
        self.limit_inflate = None
        self.stopped = False

    def map_image(self, d):
        base, size, secs, _ = pe_sections(d)
        size = (size + 0xFFF) & ~0xFFF
        self.uc.mem_map(base, size)
        self.uc.mem_write(base, d[:0x400])
        for va, vs, rp, rs in secs:
            n = min(vs, rs) if vs else rs
            if n: self.uc.mem_write(base + va, d[rp:rp + n])
        return base

    # ---- imports of the DLL: each slot points at a `ret N` in the hook page; the hook does the work ----
    STD = {'VirtualAlloc': 4, 'VirtualProtect': 4, 'FlushInstructionCache': 3, 'GetCurrentProcess': 0, 'Sleep': 1}
    CDECL = {'wsprintfA'}

    def fill_imports(self):
        d = self.dll
        base, size, secs, imp_rva = pe_sections(d)
        def off(rva):
            for va, vs, rp, rs in secs:
                if va <= rva < va + max(vs, rs): return rp + rva - va
            raise KeyError(hex(rva))
        o = off(imp_rva)
        idx = 0
        while True:
            oft, _, _, name, ft = struct.unpack_from('<5I', d, o)
            if not name: break
            lo = off(oft or ft)
            i = 0
            while True:
                t = struct.unpack_from('<I', d, lo + i * 4)[0]
                if not t: break
                fn = d[off(t) + 2:d.index(b'\0', off(t) + 2)].decode()
                addr = HOOKPAGE + idx * 8
                nargs = self.STD.get(fn)
                if nargs is not None: code = b'\xC2' + struct.pack('<H', nargs * 4) if nargs else b'\xC3'
                elif fn in self.CDECL: code = b'\xC3'
                else: code = b'\xF4'  # an import these tests must not reach
                self.uc.mem_write(addr, code)
                self.uc.mem_write(base + ft + i * 4, struct.pack('<I', addr))
                self.imports[addr] = fn
                idx += 1
                i += 1
            o += 20
        self.uc.hook_add(UC_HOOK_CODE, self.on_import, begin=HOOKPAGE, end=HOOKPAGE + idx * 8)

    def arg(self, i):
        esp = self.uc.reg_read(UC_X86_REG_ESP)
        return struct.unpack('<I', self.uc.mem_read(esp + 4 + 4 * i, 4))[0]

    def on_import(self, uc, addr, size, user):
        fn = self.imports.get(addr)
        if fn is None: return
        eax = 0
        if fn == 'VirtualAlloc': eax = ARENA
        elif fn == 'VirtualProtect':
            uc.mem_write(self.arg(3), struct.pack('<I', 0x20)); eax = 1
        elif fn == 'FlushInstructionCache': eax = 1
        elif fn == 'GetCurrentProcess': eax = 0xFFFFFFFF
        elif fn == 'Sleep': self.stats['sleep'] += 1
        elif fn == 'wsprintfA': uc.mem_write(self.arg(0), b'x\0'); eax = 1
        else:
            print('UNEXPECTED IMPORT', fn); self.stopped = True; uc.emu_stop(); return
        uc.reg_write(UC_X86_REG_EAX, eax)

    # ---- the C runtime and file layer of the game: entries replaced by a ret, the hook does the work ----
    def game_hooks(self):
        table = {
            0x73A8DC: (b'\xC3', self.h_new),        # operator new(size), cdecl
            0x74EE0B: (b'\xC3', self.h_free),       # operator delete / free(p), cdecl
            0x738E30: (b'\xC3', self.h_zcalloc),    # zcalloc(opaque, items, size)
            0x738E60: (b'\xC3', self.h_zcfree),     # zcfree(opaque, p)
            0x750CBF: (b'\xC3', self.h_fread),      # fread(buf, size, count, FILE*)
            0x750416: (b'\xC3', self.h_fseek),      # fseek(FILE*, offset, whence)
            0x750105: (b'\xC3', self.h_fclose),     # fclose(FILE*)
            0x6AF720: (b'\xC2\x08\x00', self.h_open),  # CSafeRead::Open(path, root), thiscall
        }
        for a, (code, fn) in table.items():
            self.uc.mem_write(a, code)
            self.uc.hook_add(UC_HOOK_CODE, lambda uc, addr, size, user, fn=fn: fn(), begin=a, end=a)
        self.uc.hook_add(UC_HOOK_CODE, self.h_inflate_entry, begin=0x6AF5B0, end=0x6AF5B0)

    def alloc(self, n):
        p = self.heap_top
        self.heap_top += (n + PAD + 0xFFF) & ~0xFFF
        assert self.heap_top < HEAP + HEAP_SIZE, 'emulated heap exhausted'
        self.uc.mem_write(p, b'\xEE' * (n + PAD))
        self.allocs[p] = n
        self.all_allocs.append((p, n))
        return p

    def h_new(self):
        self.stats['new'] += 1
        self.uc.reg_write(UC_X86_REG_EAX, self.alloc(self.arg(0)))
    def h_free(self):
        self.stats['free'] += 1
        p = self.arg(0)
        assert p in self.allocs, 'free of an unknown pointer %08X' % p
        del self.allocs[p]
    def h_zcalloc(self):
        n = self.arg(1) * self.arg(2)
        p = self.alloc(n)
        self.uc.mem_write(p, b'\0' * n)
        self.uc.reg_write(UC_X86_REG_EAX, p)
    def h_zcfree(self):
        p = self.arg(1)
        assert p in self.allocs
        del self.allocs[p]
    def h_fread(self):
        buf, size, count, h = self.arg(0), self.arg(1), self.arg(2), self.arg(3)
        f = self.files[h]
        n = max(0, min(size * count, len(f[0]) - f[1]))
        if n: self.uc.mem_write(buf, bytes(f[0][f[1]:f[1] + n]))
        self.stats['fread'].append((f[1], size * count, n))
        f[1] += n
        self.uc.reg_write(UC_X86_REG_EAX, n // size if size else 0)
    def h_fseek(self):
        h, off, whence = self.arg(0), self.arg(1), self.arg(2)
        f = self.files[h]
        f[1] = off if whence == 0 else len(f[0]) + off
        self.stats['fseek'].append((off, whence))
        self.uc.reg_write(UC_X86_REG_EAX, 0)
    def h_fclose(self):
        self.stats['close'] += 1
        self.uc.reg_write(UC_X86_REG_EAX, 0)
    def h_open(self):
        self.stats['open'] += 1
        this = self.uc.reg_read(UC_X86_REG_ECX)
        self.files[self.loose_handle][1] = 0
        self.uc.mem_write(this + 0x108, struct.pack('<I', self.loose_handle))
        self.uc.reg_write(UC_X86_REG_EAX, 1)
    def h_inflate_entry(self, uc, addr, size, user):
        self.stats['inflate_calls'] += 1
        if self.limit_inflate and self.stats['inflate_calls'] > self.limit_inflate:
            self.stopped = True
            uc.emu_stop()

    def new_file(self, data):
        h = self.next_handle
        self.next_handle += 0x10
        self.files[h] = [data, 0]
        return h

    def reset_stats(self):
        self.stats = dict(new=0, free=0, sleep=0, inflate_calls=0, fread=[], fseek=[], open=0, close=0)

    def call(self, addr, args, ecx=0):
        uc = self.uc
        esp = STACK + STACK_SIZE - 0x1000
        for a in reversed(args):
            esp -= 4
            uc.mem_write(esp, struct.pack('<I', a & 0xFFFFFFFF))
        esp -= 4
        uc.mem_write(esp, struct.pack('<I', SENTINEL))
        uc.reg_write(UC_X86_REG_ESP, esp)
        uc.reg_write(UC_X86_REG_EBP, 0x11111111)
        uc.reg_write(UC_X86_REG_EBX, 0xB0B0B0B0)
        uc.reg_write(UC_X86_REG_ESI, 0x51515151)
        uc.reg_write(UC_X86_REG_EDI, 0xD1D1D1D1)
        uc.reg_write(UC_X86_REG_ECX, ecx)
        self.stopped = False
        uc.emu_start(addr, SENTINEL)
        regs = {n: uc.reg_read(r) for n, r in (('eax', UC_X86_REG_EAX), ('ebx', UC_X86_REG_EBX), ('esi', UC_X86_REG_ESI),
                                               ('edi', UC_X86_REG_EDI), ('ebp', UC_X86_REG_EBP), ('esp', UC_X86_REG_ESP))}
        if not self.stopped:
            assert uc.reg_read(UC_X86_REG_EIP) == SENTINEL, hex(uc.reg_read(UC_X86_REG_EIP))
            assert (regs['ebx'], regs['esi'], regs['edi'], regs['ebp']) == (0xB0B0B0B0, 0x51515151, 0xD1D1D1D1, 0x11111111), regs
        return regs

    def reset_heap(self):
        self.heap_top = HEAP
        self.allocs = {}
        self.all_allocs = []

    def canaries_ok(self):
        for p, n in self.all_allocs:
            if bytes(self.uc.mem_read(p + n, PAD)) != b'\xEE' * PAD: return False
        return True

    def symbol(self, name):
        return SYMS[name]

    # the archive reader FUN_006B0000(this = index, id, u32* sizeOut), thiscall, ret 8
    def setup_index(self, arc_data):
        self.uc.mem_write(OBJ, b'\0' * 0x1000)
        self.uc.mem_write(OBJ + 4, struct.pack('<I', 0x8D8D78))
        self.arc_handle = self.new_file(arc_data)
        self.uc.mem_write(OBJ + 0x24 + 1 * 0x118 + 0x108, struct.pack('<I', self.arc_handle))

    def read_archive(self, rid):
        self.uc.mem_write(OBJ + 0x800, struct.pack('<I', 0xCCCCCCCC))
        self.reset_stats()
        r = self.call(0x6B0000, [rid, OBJ + 0x800], ecx=OBJ)
        size = struct.unpack('<I', self.uc.mem_read(OBJ + 0x800, 4))[0]
        return r['eax'], size, r['esp']

    # the loose file reader FUN_006AF370(path, u32* sizeOut, root), cdecl
    def read_loose(self, data):
        self.loose_handle = self.new_file(data)
        self.uc.mem_write(OBJ + 0x900, b'UPDATA/TEST/LOOSE.XMD\0')
        self.uc.mem_write(OBJ + 0x800, struct.pack('<I', 0xCCCCCCCC))
        self.uc.mem_write(TEB, struct.pack('<I', 0xFFFFFFFF))
        self.reset_stats()
        r = self.call(0x6AF370, [OBJ + 0x900, OBJ + 0x800, 0])
        size = struct.unpack('<I', self.uc.mem_read(OBJ + 0x800, 4))[0]
        fs0 = struct.unpack('<I', self.uc.mem_read(TEB, 4))[0]
        return r['eax'], size, fs0

def load_symbols(path):
    syms = {}
    for line in open(path):
        parts = line.split()
        if len(parts) == 3: syms[parts[2]] = int(parts[0], 16)
    return syms

SYMS = load_symbols(sys.argv[4])
INSTALL = SYMS['__ZN3ufx20InstallResourceGuardEv']

exe = open(EXE, 'rb').read()
INDEX_OFF = 0x4D7978
entries = [struct.unpack_from('<8I', exe, INDEX_OFF + i * 0x20) for i in range(18340)]
packed_ids = [i for i, e in enumerate(entries) if e[1] and e[2] != 0xFFFFFFFF and e[3] == 1]
arc = open(ARC, 'rb').read()

def record(rid):
    off = entries[rid][0]
    magic, nxt, comp, uncomp = struct.unpack_from('<4I', arc, off + 0x104)
    assert magic == 0x31505A58
    return off, comp, uncomp

failures = 0
def check(ok, what, *info):
    global failures
    if not ok:
        failures += 1
        print('FAIL', what, *info)

def reg_snapshot(uc):
    return tuple(uc.reg_read(r) for r in (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESI, UC_X86_REG_EDI,
                                         UC_X86_REG_EBP, UC_X86_REG_ESP, UC_X86_REG_EFLAGS))

print('building two machines (stock and patched)')
stock = Machine()
patched = Machine()
before = {a: bytes(patched.uc.mem_read(a, 16)) for a in (0x6B0162, 0x6AF45A, 0x6AF61D, 0x6AF6D9, 0x6B003D, 0x6AF538, 0x6B0138, 0x6AF425, 0x6B00F2, 0x6AF3EF)}
r = patched.call(INSTALL, [])
check((r['eax'] & 0xFF) == 1, 'InstallResourceGuard returns true', r)
stubs = {}
for site, n in ((0x6B0162, 5), (0x6AF45A, 5), (0x6AF61D, 7), (0x6AF6D9, 7)):
    now = bytes(patched.uc.mem_read(site, n))
    check(now[0] == 0xE9 and all(b == 0x90 for b in now[5:]), 'site patched with a jump', hex(site), now.hex())
    target = site + 5 + struct.unpack('<i', now[1:5])[0]
    check(ARENA <= target < ARENA + 0x1000, 'jump goes into the stub arena', hex(site), hex(target))
    stubs[site] = target
    print('  %08X -> stub %08X: %s' % (site, target, bytes(patched.uc.mem_read(target, 28 if n == 5 else 21)).hex()))
for a in (0x6B003D, 0x6AF538, 0x6B0138, 0x6AF425, 0x6B00F2, 0x6AF3EF):
    check(bytes(patched.uc.mem_read(a, 16)) == before[a], 'verified-only site untouched', hex(a))

# every register and the flags at the stub entry and at the place the game continues
entry_state = {}
exits = {0x6AF624: 0x6AF61D, 0x6AF6E0: 0x6AF6D9, 0x6B0053: 0x6B0162, 0x6B003D: 0x6B0162, 0x6AF3AD: 0x6AF45A, 0x6AF538: 0x6AF45A}
preserve_checks = [0]
def on_stub_entry(uc, addr, size, site):
    entry_state[site] = reg_snapshot(uc)
def on_exit(uc, addr, size, site):
    if site in entry_state:
        snap = reg_snapshot(uc)
        preserve_checks[0] += 1
        check(snap == entry_state[site], 'registers and flags preserved through the stub', hex(site), hex(addr), entry_state[site], snap)
        del entry_state[site]
for site, stub in stubs.items():
    patched.uc.hook_add(UC_HOOK_CODE, on_stub_entry, user_data=site, begin=stub, end=stub)
for ex, site in exits.items():
    patched.uc.hook_add(UC_HOOK_CODE, on_exit, user_data=site, begin=ex, end=ex)

stock.setup_index(arc)
patched.setup_index(arc)

# ---- 1. good records: the stock and the patched reader must do exactly the same ----
random.seed(1)
small = [i for i in packed_ids if record(i)[2] < 0x20000]
edge = [i for i in packed_ids if record(i)[2] % 0x20000 in (0, 1, 0x1FFFF) or 1 <= record(i)[1] % 0x10000 <= 8]
big = sorted(packed_ids, key=lambda i: record(i)[2])[-2:]
sample = sorted(set(random.sample(small, min(len(small), int(sys.argv[5]))) + edge + big + random.sample(packed_ids, min(len(packed_ids), int(sys.argv[6])))))
SLICE = len(sys.argv) > 8
if SLICE: sample = sample[int(sys.argv[7]):int(sys.argv[8])]
print('%d packed records in the archive, %d smaller than 20000h; comparing %d' % (len(packed_ids), len(small), len(sample)))
done = 0
for rid in sample:
    off, comp, uncomp = record(rid)
    truth = zlib.decompress(arc[off + 0x114:off + 0x114 + comp])
    check(len(truth) == uncomp, 'the header size is exact', rid)
    a_ptr, a_size, a_esp = stock.read_archive(rid)
    a_stats = stock.stats
    b_ptr, b_size, b_esp = patched.read_archive(rid)
    b_stats = patched.stats
    a_buf = bytes(stock.uc.mem_read(a_ptr, uncomp)) if a_ptr else None
    b_buf = bytes(patched.uc.mem_read(b_ptr, uncomp)) if b_ptr else None
    check(a_ptr and a_size == uncomp and a_buf == truth, 'stock reader unpacks the record', rid)
    check(b_ptr and b_size == uncomp and b_buf == truth, 'patched reader: same bytes and size', rid, b_ptr, b_size)
    check(a_stats == b_stats, 'patched reader: same file reads, allocations and calls', rid, a_stats, b_stats)
    check(a_esp == b_esp, 'same stack on return', rid)
    check(b_stats['sleep'] == 0 and b_stats['inflate_calls'] == 1, 'no retry code ran', rid)
    check(len(stock.allocs) == 1 and len(patched.allocs) == 1, 'only the result buffer stays allocated', rid)
    check(patched.canaries_ok() and stock.canaries_ok(), 'nothing written behind any buffer', rid)
    stock.reset_heap(); patched.reset_heap()
    done += 1
    if done % 50 == 0: print('  ...', done, flush=True)
print('good records compared: %d; stub passes checked for registers and flags: %d' % (done, preserve_checks[0]))
if SLICE:
    print('SLICE PASSED' if not failures else '%d CHECKS FAILED' % failures)
    sys.exit(1 if failures else 0)

# ---- 2. a record that does not unpack ----
def variant(rid, how):
    off, comp, uncomp = record(rid)
    data = bytearray(arc)
    if how == 'corrupt':
        for k in range(8): data[off + 0x114 + comp // 2 + k] ^= 0xFF
    elif how == 'truncated':
        data = data[:off + 0x114 + comp - 5]
    elif isinstance(how, int):   # the header declares `how` bytes fewer than the stream holds
        struct.pack_into('<I', data, off + 0x110, uncomp - how)
    return bytes(data)

mids = [i for i in packed_ids if 0x48000 < record(i)[2] < 0x60000]
rid_big = mids[0]
fresh = iter(small[100:140] )
fresh_big = iter(mids[1:20])
for how in ('corrupt', 'truncated'):
    rid = next(fresh)
    for m, name in ((stock, 'stock'), (patched, 'patched')):
        m.files[m.arc_handle][0] = variant(rid, how)
        m.limit_inflate = 60
        ptr, size, _ = m.read_archive(rid)
        m.limit_inflate = None
        s = m.stats
        if name == 'stock':
            check(m.stopped and s['inflate_calls'] > 60, 'stock reader loops without end on a record that does not unpack', how, s['inflate_calls'])
            print('  stock, %s record: still retrying after %d unpack calls (stopped by the test)' % (how, s['inflate_calls'] - 1))
        else:
            check(not m.stopped and ptr == 0 and size == 0, 'patched reader gives up: returns 0, size 0', how, ptr, size)
            check(s['inflate_calls'] == 5 and s['sleep'] == 4, 'patched reader: 5 reads, 4 pauses', how, s['inflate_calls'], s['sleep'])
            check(s['new'] == 5 and s['free'] == 5 and not m.allocs, 'patched reader: every buffer freed', how, s, len(m.allocs))
            print('  patched, %s record: %d unpack calls, %d pauses, returns %X, size %d' % (how, s['inflate_calls'], s['sleep'], ptr, size))
            ptr, size, _ = m.read_archive(rid)
            s = m.stats
            check(ptr == 0 and size == 0 and s['inflate_calls'] == 1 and s['sleep'] == 0, 'patched reader: a known bad record costs one read', how, s['inflate_calls'], s['sleep'])
        m.reset_heap()
        m.files[m.arc_handle][0] = arc
    # after the archive is whole again the same record reads fine (nothing sticks)
    ptr, size, _ = patched.read_archive(rid)
    check(ptr != 0 and size == record(rid)[2] and patched.stats['inflate_calls'] == 1, 'patched reader: the record reads again once the file is whole', how)
    patched.reset_heap()

# ---- 3. a stream that holds more than its header declares ----
for pick in (fresh, fresh_big):
  for which in range(4):
    target = next(pick)
    off, comp, uncomp = record(target)
    for less in ((1, 300, uncomp // 2, uncomp - 16)[which],):
        if less >= uncomp: continue
        data = variant(target, less)
        m = stock
        m.files[m.arc_handle][0] = data
        m.limit_inflate = 3
        ptr, size, _ = m.read_archive(target)
        m.limit_inflate = None
        stock_overflow = not m.canaries_ok()
        m.reset_heap(); m.files[m.arc_handle][0] = arc
        m = patched
        m.files[m.arc_handle][0] = data
        ptr, size, _ = m.read_archive(target)
        s = m.stats
        check(stock_overflow, 'stock reader writes behind the buffer', target, less)
        check(m.canaries_ok(), 'patched reader: nothing behind the buffer', target, less)
        check(ptr == 0 and size == 0 and s['inflate_calls'] == 5, 'patched reader: oversized stream is given up after 5 reads', target, less, ptr, size, s['inflate_calls'])
        print('  record %04X, header %d bytes short: stock overflows %s; patched overflows %s, returns %X after %d reads' %
              (target, less, stock_overflow, not m.canaries_ok(), ptr, s['inflate_calls']))
        m.reset_heap(); m.files[m.arc_handle][0] = arc
        # the known-bad table must not stick to the id once the data is whole
        ptr, size, _ = m.read_archive(target)
        check(ptr != 0 and size == uncomp, 'patched reader: whole record reads again', target)
        m.reset_heap()

# ---- 4. the loose file reader ----
off, comp, uncomp = record(rid_big)
loose = arc[off + 0x104:off + 0x114 + comp]
truth = zlib.decompress(loose[16:])
for m, name in ((stock, 'stock'), (patched, 'patched')):
    m.reset_heap()
    ptr, size, fs0 = m.read_loose(loose)
    check(ptr != 0 and size == uncomp and bytes(m.uc.mem_read(ptr, uncomp)) == truth, name + ': loose XZP1 file unpacks', ptr, size)
    check(fs0 == 0xFFFFFFFF, name + ': exception chain restored', hex(fs0))
    check(m.stats['inflate_calls'] == 1 and m.stats['sleep'] == 0 and m.stats['open'] == 1, name + ': one read', m.stats)
    if name == 'stock': loose_stats = m.stats
    else: check(m.stats == loose_stats, 'loose file: patched does the same as stock', loose_stats, m.stats)
    m.reset_heap()
bad = bytearray(loose)
for k in range(8): bad[16 + comp // 2 + k] ^= 0xFF
stock.limit_inflate = 40
ptr, size, fs0 = stock.read_loose(bytes(bad))
stock.limit_inflate = None
check(stock.stopped, 'stock: loose file that does not unpack loops without end')
stock.reset_heap()
ptr, size, fs0 = patched.read_loose(bytes(bad))
s = patched.stats
check(not patched.stopped and ptr == 0 and size == 0 and fs0 == 0xFFFFFFFF, 'patched: loose file given up cleanly', ptr, size, hex(fs0))
check(s['inflate_calls'] == 5 and s['sleep'] == 4 and s['open'] == 5 and s['close'] == 5 and s['new'] == 5 and s['free'] == 5 and not patched.allocs,
      'patched: loose file read 5 times, everything closed and freed', s, len(patched.allocs))
print('  patched, loose file that does not unpack: %d reads, returns %X, size %d' % (s['inflate_calls'], ptr, size))
short = bytearray(loose)
struct.pack_into('<I', short, 12, uncomp - 1000)
ptr, size, fs0 = patched.read_loose(bytes(short))
check(ptr == 0 and size == 0 and patched.canaries_ok(), 'patched: oversized loose stream stopped at the buffer', ptr, size)
patched.reset_heap()

# ---- 5. made-up streams at the edges (around the 20000h output room and the 10000h input chunk) through both readers ----
def make(n, compressible, seed):
    rnd = random.Random(seed)
    if compressible: return bytes(((i // 97) ^ (rnd.getrandbits(1))) & 0xFF for i in range(n))
    return bytes(rnd.getrandbits(8) for _ in range(n))
def xzp1(data, level):
    z = zlib.compress(data, level)
    return struct.pack('<4I', 0x31505A58, (16 + len(z) + 15) & ~15, len(z), len(data)) + z, len(z)
SYN_ID = 3
for m in (stock, patched): m.uc.mem_write(0x8D8D78 + SYN_ID * 0x20, struct.pack('<4I', 0, 1, 1, 1))
cases = []
for n in (0, 1, 2, 257, 258, 259, 65535, 65536, 0x1FFFF, 0x20000, 0x20001, 0x20000 + 258, 0x3FFFF, 0x40000, 0x40001):
    for compressible in (False, True):
        for level in (0, 9):
            cases.append((n, compressible, level))
tails = 0
for n in range(2 * 0x10000 - 40, 2 * 0x10000 + 8):
    z = zlib.compress(make(n, False, 7), 0)
    if 1 <= len(z) % 0x10000 <= 8:
        cases.append((n, False, 0)); tails += 1
syn = 0
for n, compressible, level in cases:
    data = make(n, compressible, 7 if level == 0 and not compressible else n + level)
    blob, zlen = xzp1(data, level)
    filedata = b'\0' * 0x104 + blob
    res = []
    for m in (stock, patched):
        m.files[m.arc_handle][0] = filedata
        ptr, size, _ = m.read_archive(SYN_ID)
        buf = bytes(m.uc.mem_read(ptr, n)) if ptr else None
        res.append((ptr != 0, size, buf, m.stats, m.canaries_ok() if m is patched else True))
        m.reset_heap()
        ptr, size, fs0 = m.read_loose(blob)
        buf = bytes(m.uc.mem_read(ptr, n)) if ptr else None
        res.append((ptr != 0, size, buf, m.stats, m.canaries_ok() if m is patched else True, fs0))
        m.reset_heap()
        m.files[m.arc_handle][0] = arc
    check(res[0][0] and res[0][1] == n and res[0][2] == data, 'made-up stream: stock archive reader unpacks it', n, compressible, level)
    check(res[2][:4] == res[0][:4] and res[2][4], 'made-up stream: patched archive reader does the same', n, compressible, level, zlen, res[2][0], res[2][1])
    check(res[1][0] and res[1][1] == n and res[1][2] == data, 'made-up stream: stock loose reader unpacks it', n, compressible, level)
    check(res[3][:4] == res[1][:4] and res[3][4] and res[3][5] == 0xFFFFFFFF, 'made-up stream: patched loose reader does the same', n, compressible, level, zlen)
    syn += 1
print('made-up edge streams compared through both readers: %d (%d with the trailer behind an input chunk boundary)' % (syn, tails))

counts = [struct.unpack('<i', patched.uc.mem_read(SYMS[n], 4))[0] for n in
          ('__ZN3ufx12_GLOBAL__N_1L9g_retriesE', '__ZN3ufx12_GLOBAL__N_1L9g_givenUpE', '__ZN3ufx12_GLOBAL__N_1L11g_overflowsE')]
print('counters of the mod: repeated reads %d, given up %d, streams stopped at the buffer %d' % tuple(counts))
print('registers/flags checks through the stubs: %d' % preserve_checks[0])
print('ALL EMULATION CHECKS PASSED' if not failures else '%d CHECKS FAILED' % failures)
sys.exit(1 if failures else 0)

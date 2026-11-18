// Builds every machine-code stub of stubs.h at a made-up address, writes each as a raw binary for objdump (when
// file names are given), and checks for every builder its size, every jump or call to an address outside the stub
// (decoded from the rel32 or the absolute operand and compared with the address it must reach), and the short
// jumps inside the stub. `make test` runs the checks; it does not disassemble the files.
#include <stdio.h>
#include <string.h>
#include "sites.h"
#include "stubs.h"
using namespace ufx;

static int g_failures = 0;

static void Check(bool ok, const char* stub, const char* what) {
    if (!ok) {
        printf("FAIL  %s: %s\n", stub, what);
        ++g_failures;
    }
}

static uint32_t Rd32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

// E9 (jmp) / E8 (call) rel32 at `off`, or 0F 8x (jcc) rel32: does it reach `target`?
static bool Rel32To(const uint8_t* b, uint32_t va, size_t off, uint32_t target) {
    if (b[off] == 0xE9 || b[off] == 0xE8) return va + (uint32_t)off + 5 + Rd32(b + off + 1) == target;
    if (b[off] == 0x0F && (b[off + 1] & 0xF0) == 0x80) return va + (uint32_t)off + 6 + Rd32(b + off + 2) == target;
    return false;
}

// Short jump (7x / EB) at `off`: does it land at offset `to` of the same stub?
static bool Rel8To(const uint8_t* b, size_t off, size_t to) {
    if (!((b[off] & 0xF0) == 0x70 || b[off] == 0xEB)) return false;
    return off + 2 + (size_t)(int8_t)b[off + 1] == to;
}

static void Save(int argc, char** argv, int index, const uint8_t* b, size_t n) {
    if (argc <= index) return;
    FILE* f = fopen(argv[index], "wb");
    if (!f) return;
    fwrite(b, 1, n, f);
    fclose(f);
}

int main(int argc, char** argv) {
    // Zero-delta stub: jz over the division, two exits back to the game.
    const uint32_t zVa = 0x10000000;
    uint8_t z[128];
    size_t n = BuildZeroDeltaStub(z, zVa, kFrameDeltaVa, kZeroDeltaReturn);
    printf("zero-delta stub %zu bytes\n", n);
    Save(argc, argv, 1, z, n);
    Check(n == 51, "zero-delta", "size 51");
    Check(Rd32(z + 3) == kFrameDeltaVa, "zero-delta", "mov eax,[frameDelta]");
    Check(Rel8To(z, 9, 34), "zero-delta", "jz zero_case");
    Check(Rel32To(z, zVa, 29, kZeroDeltaReturn), "zero-delta", "jmp back (division)");
    Check(Rel32To(z, zVa, 46, kZeroDeltaReturn), "zero-delta", "jmp back (zero case)");

    // Scale thunk: push of its own `after` address, jmp to the original, jmp [retSave] at the end.
    const uint32_t sVa = 0x20000000, sRet = 0x20001000;
    uint8_t t[128];
    size_t m = BuildScaleThunk(t, sVa, kSceneInitFn, sRet, 0x20001008, 0x2000100C, kLogicalWidthVa, kLogicalHeightVa);
    printf("scale thunk %zu bytes\n", m);
    Save(argc, argv, 2, t, m);
    Check(m == 66, "scale thunk", "size 66");
    Check(t[30] == 0x68 && Rd32(t + 31) == sVa + 40, "scale thunk", "push after");
    Check(Rel32To(t, sVa, 35, kSceneInitFn), "scale thunk", "jmp original");
    Check(t[60] == 0xFF && t[61] == 0x25 && Rd32(t + 62) == sRet, "scale thunk", "jmp [retSave]");

    uint8_t b[96], h[32];
    const uint32_t bVa = 0x30000000, hVa = 0x30000100, hFn = 0x30000200;
    size_t nb = BuildBackbufferThunk(b, bVa, kFullscreenFlagVa, 1920, 1080, kResetParamsVa, kOutPresentParamsReturn);
    size_t nh = BuildHudStub(h, hVa, kHudDepthSlotVa, hFn);
    printf("backbuffer thunk %zu bytes, hud stub %zu bytes\n", nb, nh);
    if (argc > 4) {
        Save(argc, argv, 3, b, nb);
        Save(argc, argv, 4, h, nh);
    }
    Check(nb == 43, "backbuffer thunk", "size 43");
    Check(Rd32(b + 2) == kFullscreenFlagVa, "backbuffer thunk", "cmp [fullscreen]");
    Check(Rel8To(b, 7, 23), "backbuffer thunk", "jne skip");
    Check(Rd32(b + 32) == kResetParamsVa, "backbuffer thunk", "mov edi, Reset parameters");
    Check(Rel32To(b, bVa, 38, kOutPresentParamsReturn), "backbuffer thunk", "jmp back");
    Check(nh == 16, "hud stub", "size 16");
    Check(Rd32(h + 2) == kHudDepthSlotVa, "hud stub", "mov [slot], esi");
    Check(Rel32To(h, hVa, 8, hFn), "hud stub", "call fn");

    uint8_t gd[32], gt[32], ah[96], av[48];
    const uint32_t tVa = 0x40000100, ahVa = 0x40000200, avVa = 0x40000300, eps = 0x40001000;
    size_t nd = BuildDifficultyStore(gd, 0x40000000, kDifficultyOffset);
    size_t nt = BuildTitleDialogStub(gt, tVa, kTitleStateVa, kTitleCursorVa, kDifficultyDialog1Return);
    size_t nah = BuildAimHorizontal(ah, ahVa, eps, kAimHNotEqual, kAimHEqual);
    size_t nav = BuildAimVertical(av, avVa, eps, kAimVExit);
    printf("difficulty store %zu, title dialog %zu, aim H %zu, aim V %zu bytes\n", nd, nt, nah, nav);
    if (argc > 8) {
        Save(argc, argv, 5, gd, nd);
        Save(argc, argv, 6, gt, nt);
        Save(argc, argv, 7, ah, nah);
        Save(argc, argv, 8, av, nav);
    }
    Check(nd == 15, "difficulty store", "size 15");
    Check(Rd32(gd + 9) == kDifficultyOffset, "difficulty store", "mov [eax+offset], cl");
    Check(nt == 25, "title dialog", "size 25");
    Check(Rel32To(gt, tVa, 20, kDifficultyDialog1Return), "title dialog", "jmp back");
    Check(nah == 64, "aim H", "size 64");
    Check(Rel8To(ah, 19, 23) && Rel8To(ah, 21, 55), "aim H", "jp / jbe of the first test");
    Check(Rel8To(ah, 42, 46) && Rel8To(ah, 44, 55), "aim H", "jp / jbe of the second test");
    Check(Rel32To(ah, ahVa, 50, kAimHNotEqual), "aim H", "jmp not equal");
    Check(Rel32To(ah, ahVa, 59, kAimHEqual), "aim H", "jmp equal");
    Check(nav == 32, "aim V", "size 32");
    Check(Rel8To(av, 21, 27) && Rel8To(av, 23, 27), "aim V", "jp / ja done");
    Check(Rel32To(av, avVa, 27, kAimVExit), "aim V", "jmp exit");

    uint8_t so[16], as[128];
    const uint32_t soVa = 0x50000000, asVa = 0x50000100, asRet = 0x50001000;
    size_t nso = BuildStickOriginal(so, soVa, kStickEvalReturn);
    size_t nas = BuildAimShapeThunk(as, asVa, kStickGetterFn, asRet, kUseJoyVa, 0x50001004, 0x50001008, 0x5000100C);
    printf("stick original %zu, aim shape thunk %zu bytes\n", nso, nas);
    if (argc > 10) {
        Save(argc, argv, 9, so, nso);
        Save(argc, argv, 10, as, nas);
    }
    Check(nso == 11 && memcmp(so, kSiteStickEval.bytes, 6) == 0, "stick original", "size 11, replayed prologue");
    Check(Rel32To(so, soVa, 6, kStickEvalReturn), "stick original", "jmp back");
    Check(nas == 88, "aim shape", "size 88");
    Check(as[6] == 0x68 && Rd32(as + 7) == asVa + 16, "aim shape", "push after");
    Check(Rel32To(as, asVa, 11, kStickGetterFn), "aim shape", "jmp original");
    Check(as[nas - 6] == 0xFF && as[nas - 5] == 0x25 && Rd32(as + nas - 4) == asRet, "aim shape", "jmp [retSave] at the end");
    Check(Rel8To(as, 23, nas - 6) && Rel8To(as, 32, nas - 6), "aim shape", "je done (controller mode, profile)");

    uint8_t vb[32];
    const uint32_t vbVa = 0x60000000;
    size_t nvb = BuildVisibilityBridge(vb, vbVa, 0x60001000, 1, kVisibilityTestFn);
    printf("visibility bridge %zu bytes\n", nvb);
    Save(argc, argv, 11, vb, nvb);
    Check(nvb == 19, "visibility bridge", "size 19");
    Check(Rel8To(vb, 7, 14), "visibility bridge", "jz orig");
    Check(Rel32To(vb, vbVa, 14, kVisibilityTestFn), "visibility bridge", "jmp original");

    uint8_t t1[96], t2[96], t3[96];
    const uint32_t t1Va = 0x70000000, t2Va = 0x70000100, t3Va = 0x70000200;
    size_t n1 = BuildTrigStub1(t1, t1Va, kUseJoyVa, 0x70001000, 0x70001004, 0x70001008, kTrig1Keyboard, kTrig1Pad, kTrig1Merge);
    size_t n2 = BuildTrigStub2(t2, t2Va, kUseJoyVa, 0x70001000, 0x70001004, 0x70001008, kTrig2Resume, kTrig2Merge);
    size_t n3 = BuildTrigStub3(t3, t3Va, kUseJoyVa, 0x70001000, 0x70001004, 0x70001008, kTrig3Keyboard, kTrig3Pad, kTrig3Merge);
    printf("trigger stubs %zu %zu %zu bytes\n", n1, n2, n3);
    if (argc > 14) {
        Save(argc, argv, 12, t1, n1);
        Save(argc, argv, 13, t2, n2);
        Save(argc, argv, 14, t3, n3);
    }
    Check(n1 == 51, "trigger stub 1", "size 51");
    Check(Rel32To(t1, t1Va, 7, kTrig1Keyboard), "trigger stub 1", "je keyboard");
    Check(Rel32To(t1, t1Va, 20, kTrig1Pad), "trigger stub 1", "je controller branch");
    Check(Rel32To(t1, t1Va, 46, kTrig1Merge), "trigger stub 1", "jmp merge");
    Check(n2 == 61, "trigger stub 2", "size 61");
    Check(Rel8To(t2, 7, 25), "trigger stub 2", "je back");
    Check(Rel8To(t2, 16, 30), "trigger stub 2", "jne analog");
    Check(Rel32To(t2, t2Va, 25, kTrig2Resume), "trigger stub 2", "jmp resume");
    Check(Rel32To(t2, t2Va, 56, kTrig2Merge), "trigger stub 2", "jmp merge");
    Check(n3 == 53, "trigger stub 3", "size 53");
    Check(Rel32To(t3, t3Va, 7, kTrig3Keyboard), "trigger stub 3", "je keyboard");
    Check(Rel32To(t3, t3Va, 20, kTrig3Pad), "trigger stub 3", "je controller branch");
    Check(Rel32To(t3, t3Va, 48, kTrig3Merge), "trigger stub 3", "jmp merge");

    uint8_t g1[128], g2[32];
    const uint32_t g1Va = 0x80000000, g2Va = 0x80000100;
    size_t ng1 = BuildNavLookupGuard(g1, g1Va, kNavLookupContinue);
    size_t ng2 = BuildNavNullGuard(g2, g2Va, kNavNullContinue);
    printf("nav guards %zu %zu bytes\n", ng1, ng2);
    if (argc > 16) {
        Save(argc, argv, 15, g1, ng1);
        Save(argc, argv, 16, g2, ng2);
    }
    Check(ng1 == 86, "nav lookup guard", "size 86");
    Check(Rel8To(g1, 4, 11), "nav lookup guard", "jne active");
    Check(Rel32To(g1, g1Va, 18, kNavLookupContinue), "nav lookup guard", "jbe original");
    Check(Rel32To(g1, g1Va, 29, kNavLookupContinue), "nav lookup guard", "je original (7FFFFEh)");
    Check(Rel32To(g1, g1Va, 40, kNavLookupContinue), "nav lookup guard", "je original (7FFFF7h)");
    Check(Rel8To(g1, 52, 81), "nav lookup guard", "jz noout");
    Check(ng2 == 21, "nav NULL guard", "size 21");
    Check(Rel8To(g2, 2, 14), "nav NULL guard", "jz zero");
    Check(Rel32To(g2, g2Va, 9, kNavNullContinue) && Rel32To(g2, g2Va, 16, kNavNullContinue), "nav NULL guard", "both jmp continue");

    uint8_t cb[32];
    const uint32_t cbVa = 0x90000000, cbWorker = 0x90001000;
    size_t ncb = BuildCameraBridge(cb, cbVa, cbWorker, kCameraBuildFn);
    printf("camera bridge %zu bytes\n", ncb);
    Save(argc, argv, 17, cb, ncb);
    Check(ncb == 16, "camera bridge", "size 16");
    Check(Rel32To(cb, cbVa, 3, cbWorker), "camera bridge", "call worker");
    Check(Rel32To(cb, cbVa, 11, kCameraBuildFn), "camera bridge", "jmp original");

    uint8_t sb[48];
    const uint32_t sbVa = 0xA0000000, sbWorker = 0xA0001000;
    size_t nsb = BuildStrafeBridge(sb, sbVa, sbWorker, kStrafeReturn);
    printf("strafe bridge %zu bytes\n", nsb);
    Save(argc, argv, 18, sb, nsb);
    Check(nsb == 25, "strafe bridge", "size 25");
    Check(Rel32To(sb, sbVa, 3, sbWorker), "strafe bridge", "call worker");
    Check(memcmp(sb + 13, kSiteStrafeTail.bytes, 7) == 0, "strafe bridge", "replayed compare");
    Check(Rel32To(sb, sbVa, 20, kStrafeReturn), "strafe bridge", "jmp back");

    // Quick-turn gate: the worker call, the six replayed bytes, the relocated flag test and both exits.
    uint8_t qt[48];
    const uint32_t qtVa = 0xA8000000, qtWorker = 0xA8001000;
    size_t nqt = BuildQuickTurnGate(qt, qtVa, qtWorker, kFlagTestFn, kQuickTurnResume, kQuickTurnExit);
    printf("quick-turn gate %zu bytes\n", nqt);
    Check(nqt == 34, "quick-turn gate", "size 34");
    Check(Rel32To(qt, qtVa, 1, qtWorker), "quick-turn gate", "call worker");
    Check(Rel8To(qt, 11, 29), "quick-turn gate", "jnz started");
    Check(memcmp(qt + 13, kSiteQuickTurnGate.bytes, 6) == 0, "quick-turn gate", "replayed push 0 / push 4 / mov ecx,esi");
    Check(Rel32To(qt, qtVa, 19, kFlagTestFn), "quick-turn gate", "call flag test");
    Check(kSiteQuickTurnGate.va + 11 + Rd32(kSiteQuickTurnGate.bytes + 7) == kFlagTestFn, "quick-turn gate", "the replaced call was the flag test");
    Check(Rel32To(qt, qtVa, 24, kQuickTurnResume), "quick-turn gate", "jmp resume");
    Check(kQuickTurnResume == kSiteQuickTurnGate.va + kSiteQuickTurnGate.len, "quick-turn gate", "resume right behind the site");
    Check(Rel32To(qt, qtVa, 29, kQuickTurnExit), "quick-turn gate", "jmp exit");
    Check(kQuickTurnExit == kQuickTurnChecks[1].va, "quick-turn gate", "exit is the verified epilogue");

    uint8_t eh[64];
    const uint32_t ehVa = 0xB0000000, ehWorker = 0xB0001000;
    size_t neh = BuildEntryHook(eh, ehVa, ehWorker, kSiteStateEntry.bytes, kSiteStateEntry.len, kStateEntryReturn);
    printf("entry hook %zu bytes\n", neh);
    Save(argc, argv, 19, eh, neh);
    Check(neh == 29, "entry hook", "size 29");
    Check(Rel32To(eh, ehVa, 7, ehWorker), "entry hook", "call worker");
    Check(memcmp(eh + 17, kSiteStateEntry.bytes, 7) == 0, "entry hook", "replayed entry bytes");
    Check(Rel32To(eh, ehVa, 24, kStateEntryReturn), "entry hook", "jmp back");
    // The waypoint hooks the text draw entry with the same builder and 6 replayed bytes.
    uint8_t th[64];
    const SiteDef& td = kSiteTextDrawEntry;
    size_t nth = BuildEntryHook(th, ehVa, ehWorker, td.bytes, td.len, td.va + td.len);
    Check(nth == 22 + td.len && nth <= 64, "text draw hook", "size 22 + replayed bytes, fits the 64-byte stub");
    Check(Rel32To(th, ehVa, 7, ehWorker), "text draw hook", "call worker");
    Check(memcmp(th + 17, td.bytes, td.len) == 0, "text draw hook", "replayed entry bytes");
    Check(Rel32To(th, ehVa, 17 + td.len, td.va + td.len), "text draw hook", "jmp back");

    // Car tick counters: the frame delta itself outside of a step plan, the plan's ticks inside one; and every site it is called
    // from is `fld dword [frameDelta]` followed by a call (the flags the stub changes are dead there).
    uint8_t ct[32];
    const uint32_t ctSteps = 0xC0001000, ctTicks = 0xC0001004;
    size_t nct = BuildCarTicksStub(ct, ctSteps, ctTicks, kFrameDeltaVa);
    printf("car ticks stub %zu bytes\n", nct);
    Check(nct == 23, "car ticks", "size 23");
    Check(ct[0] == 0x83 && ct[1] == 0x3D && Rd32(ct + 2) == ctSteps && ct[6] == 0, "car ticks", "cmp dword [steps],0");
    Check(ct[7] == 0x7C && Rel8To(ct, 7, 16), "car ticks", "jl own");
    Check(ct[9] == 0xD9 && ct[10] == 0x05 && Rd32(ct + 11) == ctTicks && ct[15] == 0xC3, "car ticks", "fld dword [ticks] / ret");
    Check(ct[16] == 0xD9 && ct[17] == 0x05 && Rd32(ct + 18) == kFrameDeltaVa && ct[22] == 0xC3, "car ticks", "fld dword [frameDelta] / ret");
    for (const SiteDef& s : kSiteCarTicks) {
        Check(memcmp(s.bytes, ct + 16, 6) == 0, s.name, "the site is the fld the stub replays");
        Check(s.len == 11 && Rel32To(s.bytes, s.va, 6, kFtolFn), s.name, "followed by call _ftol2");
    }

    // The cue matrix sites: the bytes written over each 28-byte run of the game, built at the address of the run itself.
    uint8_t cm[kCueMatrixLen];
    const uint32_t cmHelper = 0xC0001000;
    size_t ncm = BuildCueMatrixCall(cm, kSiteCueMatrix3d.va, cmHelper, kCueMatrix3dDisp, kCueMatrixLen);
    printf("cue matrix call %zu bytes\n", ncm);
    Check(ncm == kCueMatrixLen, "cue matrix (3D)", "fills the 28 bytes of the run");
    Check(cm[0] == 0x8D && cm[1] == 0x45 && cm[2] == 0xE8, "cue matrix (3D)", "lea eax,[ebp-18h]");
    Check(Rel32To(cm, kSiteCueMatrix3d.va, 13, cmHelper), "cue matrix (3D)", "call helper");
    Check(Rel8To(cm, 18, kCueMatrixLen), "cue matrix (3D)", "jmp to the end of the run");
    ncm = BuildCueMatrixCall(cm, kSiteCueMatrix2d.va, cmHelper, kCueMatrix2dDisp, kCueMatrixLen);
    Check(ncm == kCueMatrixLen, "cue matrix (2D)", "fills the 28 bytes of the run");
    Check(cm[0] == 0x8D && cm[1] == 0x45 && cm[2] == 0xAC, "cue matrix (2D)", "lea eax,[ebp-54h]");
    Check(Rel32To(cm, kSiteCueMatrix2d.va, 13, cmHelper), "cue matrix (2D)", "call helper");
    Check(Rel8To(cm, 18, kCueMatrixLen), "cue matrix (2D)", "jmp to the end of the run");

    // Resource read guard: the retry guard with its two ways out, and the stub that sets the output room of inflate().
    uint8_t rg[48], ir[32];
    const uint32_t rgVa = 0xC0000000, rgWorker = 0xC0001000, irVa = 0xC0000100, irWorker = 0xC0001100;
    size_t nrg = BuildRetryGuard(rg, rgVa, rgWorker, kArchiveRetryTarget, kSiteArchiveMissing.va);
    size_t nir = BuildInflateRoomStub(ir, irVa, irWorker, kInflateRoomFirstReturn);
    printf("resource guard stubs %zu %zu bytes\n", nrg, nir);
    Check(nrg == 28, "retry guard", "size 28 (the module allocates 32)");
    Check(Rel32To(rg, rgVa, 3, rgWorker), "retry guard", "call worker");
    Check(Rel8To(rg, 14, 22), "retry guard", "jz giveup");
    Check(Rel32To(rg, rgVa, 17, kArchiveRetryTarget), "retry guard", "jmp retry");
    Check(Rel32To(rg, rgVa, 23, kSiteArchiveMissing.va), "retry guard", "jmp missing exit");
    Check(rg[0] == 0x9C && rg[1] == 0x60 && rg[13] == 0x61 && rg[16] == 0x9D && rg[22] == 0x9D, "retry guard", "pushfd/pushad, popad, popfd on both ways out");
    // the jumps it replaces went where it sends a retry
    Check(Rel32To(kSiteArchiveRetry.bytes, kSiteArchiveRetry.va, 0, kArchiveRetryTarget), "retry guard", "archive reader: the original jump target");
    Check(Rel32To(kSiteLooseRetry.bytes, kSiteLooseRetry.va, 0, kLooseRetryTarget), "retry guard", "loose file reader: the original jump target");
    Check(nir == 21, "inflate room", "size 21 (the module allocates 32)");
    Check(Rel32To(ir, irVa, 3, irWorker), "inflate room", "call worker");
    Check(ir[11] == 0x89 && ir[12] == 0x45 && ir[13] == 0xD4 && ir[14] == 0x61 && ir[15] == 0x9D, "inflate room", "mov [ebp-2Ch],eax / popad / popfd");
    Check(Rel32To(ir, irVa, 16, kInflateRoomFirstReturn), "inflate room", "jmp back");
    // both replaced instructions are `mov dword [ebp-2Ch], 20000h`, and execution continues right behind them
    const SiteDef* const roomSites[2] = {&kSiteInflateRoomFirst, &kSiteInflateRoomRefill};
    for (const SiteDef* s : roomSites)
        Check(s->bytes[0] == 0xC7 && s->bytes[1] == 0x45 && s->bytes[2] == 0xD4 && Rd32(s->bytes + 3) == kInflateRoomStep, "inflate room", "replaced instruction");
    Check(kInflateRoomFirstReturn == kSiteInflateRoomFirst.va + kSiteInflateRoomFirst.len &&
          kInflateRoomRefillReturn == kSiteInflateRoomRefill.va + kSiteInflateRoomRefill.len, "inflate room", "return addresses");
    // the size of the caller's buffer: the displacement of the `mov ecx,[ebp+disp32]` in front of its operator new
    Check((int32_t)Rd32(kSiteArchiveBufferSize.bytes + 2) == kArchiveUnpackedSizeOffset, "inflate room", "archive reader: buffer size offset");
    Check((int32_t)Rd32(kSiteLooseBufferSize.bytes + 2) == kLooseUnpackedSizeOffset, "inflate room", "loose file reader: buffer size offset");

    // NaN trap guard: a number goes back to 00409EA8, a NaN reports the caller and returns 0.0 (fldz / ret)
    {
        uint8_t nt[32];
        const uint32_t ntVa = 0xC0000200, ntWorker = 0xC0001200;
        size_t nnt = BuildNanTrapGuard(nt, ntVa, ntWorker, kNanTrapNumber);
        printf("nan trap guard %zu bytes\n", nnt);
        Check(nnt == 23, "nan trap", "size 23 (the module allocates 32)");
        Check(nt[0] == 0x85 && nt[1] == 0xC0 && nt[2] == 0x74 && Rel8To(nt, 2, 18), "nan trap", "test eax,eax / jz number");
        Check(nt[4] == 0xFF && nt[5] == 0x34 && nt[6] == 0x24, "nan trap", "push dword [esp]");
        Check(Rel32To(nt, ntVa, 7, ntWorker), "nan trap", "call worker");
        Check(nt[12] == 0x83 && nt[13] == 0xC4 && nt[14] == 0x04 && nt[15] == 0xD9 && nt[16] == 0xEE && nt[17] == 0xC3, "nan trap", "add esp,4 / fldz / ret");
        Check(Rel32To(nt, ntVa, 18, kNanTrapNumber), "nan trap", "jmp number");
        Check(kSiteNanTrap.bytes[2] == 0x74 && kSiteNanTrap.va + 4 + (int8_t)kSiteNanTrap.bytes[3] == kNanTrapNumber, "nan trap", "the original jz goes where a number continues");
        Check(kSiteNanTrap.bytes[4] == 0xEB && kSiteNanTrap.bytes[5] == 0xFE, "nan trap", "the replaced jmp $");
    }

    printf("%s\n", g_failures ? "STUB CHECKS FAILED" : "all stub checks passed");
    return g_failures ? 1 : 0;
}

// Small byte-level fixes: HOUSE_LIST.NOD byte order, the stray joyGetPosEx call, skip intro.
#include "../base.h"
#include "../sites.h"

namespace ufx {

// HOUSE_LIST.NOD is 81 records of 0x50 bytes; only the first 16-bit field (the key) is multi-byte.
// The port swaps with a format describing 0x20 bytes per record, so 64 of 81 records end up with a
// wrong key and the building day/night state is never found. This format steps 0x50 bytes per
// record: one 16-bit swap followed by 78 untouched bytes.
#define T10 "1111111111"
static const char kHouseListFormat[] = "2" T10 T10 T10 T10 T10 T10 T10 "11111111";
#undef T10

bool InstallHouseList() {
    const SiteDef& s = kSiteHouseListFormat;
    if (!BytesMatch(s.va, s.bytes, s.len)) {
        Log("house-list: bytes at %08X differ from DP.exe 1.01b, skipped", s.va);
        return false;
    }
    u32 fmt = (u32)(uintptr_t)kHouseListFormat;
    u8 operand[4] = {(u8)fmt, (u8)(fmt >> 8), (u8)(fmt >> 16), (u8)(fmt >> 24)};
    if (!WriteCode(s.va + 1, operand, 4)) {
        Log("house-list: could not write the format pointer");
        return false;
    }
    Log("house-list: format string redirected (%d chars)", (int)(sizeof(kHouseListFormat) - 1));
    return true;
}

// FUN_00401A70 ends every frame with joyGetPosEx(0, &<uninitialised stack struct>) whose result is
// never read. Removing lea/push/push/call keeps the stack balanced (the call is stdcall).
bool InstallJoyPoll() {
    const SiteDef& s = kSiteExtraJoyPoll;
    if (!BytesMatch(s.va, s.bytes, s.len)) {
        Log("joy-poll: bytes at %08X differ from DP.exe 1.01b, skipped", s.va);
        return false;
    }
    u8 nops[16];
    for (int i = 0; i < 16; ++i) nops[i] = 0x90;
    if (!WriteCode(s.va, nops, s.len)) {
        Log("joy-poll: could not patch %08X", s.va);
        return false;
    }
    Log("joy-poll: stray joyGetPosEx call removed");
    return true;
}

bool InstallSkipIntro() {
    const SiteDef& s = kSiteSkipIntro;
    // The state value (byte 6 of the instruction) may already be 0: a launcher or another patch did the same.
    bool match = BytesMatch(s.va, s.bytes, 6) && BytesMatch(s.va + 7, s.bytes + 7, 3);
    const u8 state = *(const u8*)(uintptr_t)(s.va + 6);
    if (!match || (state != 0xB3 && state != 0x00)) {
        Log("skip-intro: bytes at %08X differ from DP.exe 1.01b, skipped", s.va);
        return false;
    }
    if (state == 0x00) {
        Log("skip-intro: already applied by another patch");
        return true;
    }
    const u8 zero = 0x00;
    if (!WriteCode(s.va + 6, &zero, 1)) return false;
    Log("skip-intro: start state set to 0");
    return true;
}

}  // namespace ufx

namespace ufx {

// The game reads config.cnf and sets the language in FUN_006B1D80; afterwards the slot is forced to US (6).
template <size_t N>
static void ApplyPadBindings(const PadBinding (&table)[N], const char* name) {
    for (const PadBinding& b : table)
        if (*(volatile u32*)(uintptr_t)b.va != 0) return;  // the file exists (some code is set): leave it alone
    for (const PadBinding& b : table) *(volatile u32*)(uintptr_t)b.va = b.code;
    Log("input: %s is missing, the Xbox 360 button layout is used", name);
}

static void __cdecl LanguageSetupHook() {
    ((void(__cdecl*)())(uintptr_t)kLanguageSetupFn)();
    if (g_cfg.forceUsSlot) *(volatile u32*)(uintptr_t)kLanguageSlotVa = 6;
    if (g_cfg.defaultPadBindings) {
        ApplyPadBindings(kPadBindingsJ, "configJ.cnf");
        ApplyPadBindings(kPadBindingsJex, "configJex.cnf");
    }
}

bool InstallEffectStep() {
    const SiteDef& a = kSiteEffectStepA;
    const SiteDef& b = kSiteEffectStepB;
    if (!BytesMatch(a.va, a.bytes, a.len) || !BytesMatch(b.va, b.bytes, b.len) || !BytesMatch(b.va + 16, kEffectStepBTail, 8)) {
        Log("effects: the effect update code differs from DP.exe 1.01b, skipped");
        return false;
    }
    if (!WriteCode(a.va, kEffectStepAFixed, sizeof(kEffectStepAFixed)) || !WriteCode(b.va, kEffectStepBFixed, sizeof(kEffectStepBFixed))) return false;
    Log("effects: muzzle flashes, splashes and hits step by 0.5 like on the console");
    return true;
}

// FUN_00459F70 turns a text token slot into an icon cell: it reads the button code bound to the slot and maps 0..5 and the
// two trigger codes 31h / 32h to cells; every other code (10 and 11, the L2/R2 buttons of the new layout, too) falls into the
// default case and comes back as the cell 8 (the right arrow of the D-pad). So the bound code is read here instead.
static unsigned __cdecl GlyphIndexHook(unsigned slot) {
    unsigned r = ((unsigned(__cdecl*)(unsigned))(uintptr_t)kGlyphIndexFn)(slot);
    static const u32 slotBinding[0x15] = {0xBE58EC, 0xBE58E8, 0xBE58F0, 0xBE58F4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                          0xBE5904, 0xBE58FC, 0xBE5900, 0xBE58F8};
    if (slot < 0x15 && slotBinding[slot]) {
        unsigned code = *(volatile u32*)(uintptr_t)slotBinding[slot];
        if (code == 10) return 0x13;  // LT icon
        if (code == 11) return 0x14;  // RT icon
    }
    return r;
}

bool InstallGlyphIndexFix() {
    const SiteDef& s = kSiteGlyphIndexCall;
    if (!BytesMatch(s.va, s.bytes, s.len)) {
        Log("icons: the icon index call at %08X differs from DP.exe 1.01b, skipped", s.va);
        return false;
    }
    if (!PatchCall(s.va, s.bytes, s.len, (const void*)GlyphIndexHook)) return false;
    Log("icons: buttons 10 and 11 (L2, R2) show the LT and RT icons");
    return true;
}

bool InstallByteTextFix() {
    const SiteDef& s = kSiteByteText;
    if (!BytesMatch(s.va, s.bytes, s.len)) {
        Log("byte text: the code at %08X differs from DP.exe 1.01b, skipped", s.va);
        return false;
    }
    if (!WriteCode(s.va, kByteTextFixed, sizeof(kByteTextFixed))) return false;
    Log("byte text: characters of 80h and above are read as unsigned");
    return true;
}

bool InstallLanguageSlot() {
    const SiteDef& s = kSiteLanguageCall;
    if (!BytesMatch(s.va, s.bytes, s.len)) {
        Log("language: bytes at %08X differ from DP.exe 1.01b, skipped", s.va);
        return false;
    }
    if (!PatchCall(s.va, s.bytes, s.len, (const void*)LanguageSetupHook)) return false;
    Log("language: configuration hook installed (US slot %s, default pad bindings %s)", g_cfg.forceUsSlot ? "forced" : "not forced",
        g_cfg.defaultPadBindings ? "on" : "off");
    return true;
}

}  // namespace ufx

namespace ufx {

// See kSiteResetRefresh in sites.h. Six NOPs leave the parameter block exactly as CreateDevice got it.
bool InstallResetRefreshFix() {
    const SiteDef& s = kSiteResetRefresh;
    if (!BytesMatch(s.va, s.bytes, s.len)) {
        Log("reset-refresh: bytes at %08X differ from DP.exe 1.01b, skipped", s.va);
        return false;
    }
    u8 nops[6] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
    if (!WriteCode(s.va, nops, s.len)) return false;
    Log("reset-refresh: desktop refresh rate no longer copied into the Reset parameters");
    return true;
}

}  // namespace ufx

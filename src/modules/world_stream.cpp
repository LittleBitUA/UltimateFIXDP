// The world that stops loading after a scene ("all the houses are gone").
//
// CGame+8C5ECh holds the game's flags; bit 1 (with bits 2 and 15: 8006h together) means "the open world is streamed", and
// FUN_004508D0 asks it in 160 places: without it no building of Greenvale is loaded. While a SCENE demo plays, the game sets
// or clears those bits EVERY FRAME from an animated value of the scene (004252B3: FUN_0041C250(scene, 45h); equal to 0.0 ->
// "and not 8006h", anything else -> "or 8006h"), and whatever the last frame of the scene left stays for the game after it.
// A player's save (0.7.13, chapter 1, the morning after the arrival) had the bits clear one game minute before the point
// where a save of the same place made without the mod has them set (flags 42000000h against 4B018006h): with the mod the
// scene's last evaluation gave 0. The unmodded game shows the houses with that very save, because the next scene sets
// the bits again at 60 frames a second.
//
// What this does: (1) while a scene plays it watches the bits; when the scene ends with them clear, but they were set until
// its last moments (cleared for less than 150 ms after at least a second of being set), they are put back: that is the
// signature of one stray evaluation at the end, not of a scene that really leaves the world closed; every change goes to
// the log, so a user's log shows what happened. (2) "Bring the buildings back" on the Fixes page sets the bits by hand for
// a game that is already in that state. (3) In free play outdoors, past the prologue, with the bits clear for ten seconds,
// a notice points at that row.
#include "../base.h"
#include "../sites.h"

namespace ufx {
namespace {

constexpr u32 kFlagsOff = 0x8C5EC, kStreamBits = 0x8006, kZoneOff = 0x834FE0, kChapterOff = 0x8C609;
constexpr u32 kSceneFlagVa = 0x8A6074, kSceneIdVa = 0x8A6078;
volatile int g_repair = 0;

}  // namespace

void WorldStreamRepair() { g_repair = 1; }

void WorldStreamFrame() {
    u8* game = *(u8* volatile*)(uintptr_t)kGameManagerPtrVa;
    if (!game || IsBadReadPtr(game + kFlagsOff, 4)) return;
    volatile u32* flags = (volatile u32*)(game + kFlagsOff);
    const bool scene = *(volatile int*)(uintptr_t)kSceneFlagVa < 0;
    const int sceneId = *(volatile int*)(uintptr_t)kSceneIdVa;
    const DWORD now = GetTickCount();
    const bool on = (*flags & 2u) != 0;
    static bool wasScene = false, wasOn = false, seen = false;
    static DWORD onSince = 0, offSince = 0, onLength = 0;
    static u32 bitsBefore = 0, lastFlags = 0;
    static int lines = 0;
    if (!seen) {
        seen = true;
        wasOn = on;
        lastFlags = *flags;
        onSince = offSince = now;
    }
    if (on != wasOn) {
        if (on) onSince = now;
        else {
            offSince = now;
            onLength = now - onSince;
            bitsBefore = lastFlags & kStreamBits;
        }
        if (lines < 120) {
            ++lines;
            Log("world stream: %s (flags %08X -> %08X) %s, scene id %d", on ? "on" : "off", lastFlags, *flags, scene ? "in a scene" : "outside scenes", sceneId);
        }
    }
    if (wasScene && !scene) {
        const bool stray = !on && offSince && now - offSince < 150 && onLength >= 1000;
        if (lines < 120) {
            ++lines;
            Log("world stream: scene %d over with the world %s (flags %08X)%s", sceneId, on ? "streamed" : "not streamed", *flags,
                stray ? ": it was streamed until the scene's last moments" : "");
        }
        if (stray && g_cfg.sceneStreamFix) {
            *flags |= bitsBefore ? bitsBefore : kStreamBits;
            Log("world stream: the bits %04X put back (the scene's last evaluation cleared them %u ms before its end, after %u ms of being set)",
                bitsBefore ? bitsBefore : kStreamBits, (u32)(now - offSince), (u32)onLength);
        }
    }
    wasScene = scene;
    wasOn = (*flags & 2u) != 0;
    lastFlags = *flags;

    const bool freePlay = *(volatile u32*)(uintptr_t)kGameplayModeVa == 3 && !scene;
    if (g_repair) {
        g_repair = 0;
        if (!freePlay) {
            ShowNotice(L"Будівлі: не зараз", L"Buildings: not now", L"Потрібна вільна гра, не сцена й не меню гри.", L"It needs free play, no scene and no game menu.", 4);
        } else {
            const u32 before = *flags;
            *flags |= kStreamBits;
            Log("world stream: repair asked: flags %08X -> %08X", before, *flags);
            ShowNotice(L"Світ знову підвантажується", L"The world is streamed again", L"Якщо будівлі не з'явилися одразу, зайдіть у будь-які двері й вийдіть або проїдьте трохи. Потім збережіться.",
                       L"If the buildings do not appear at once, go through any door and back, or drive a little. Then save.", 8);
        }
    }
    // the hint: outdoors in the open world (zone 0), past the prologue, the world not streamed for ten seconds
    static DWORD badSince = 0;
    static bool told = false;
    const bool bad = freePlay && !(*flags & 2u) && *(volatile int*)(game + kZoneOff) == 0 && *(volatile signed char*)(game + kChapterOff) >= 1;
    if (!bad) badSince = 0;
    else if (!badSince) badSince = now;
    else if (!told && now - badSince > 10000) {
        told = true;
        Log("world stream: free play outdoors with the world not streamed for 10 s (flags %08X): the buildings will be missing", *flags);
        ShowNotice(L"Світ не підвантажується", L"The world is not streamed", L"Якщо зникли будівлі: F10 -> Виправлення -> «Повернути будівлі».",
                   L"If the buildings are gone: F10 -> Fixes -> \"Bring the buildings back\".", 10, kNoticeWarning);
    }
}

}  // namespace ufx

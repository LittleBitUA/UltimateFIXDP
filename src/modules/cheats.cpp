// Cheats and world control: money, health, the clock, the weather, ammunition, "York to the camera".
//
// Every field and function used here was found in the static analysis of DP.exe 1.01b (docs/engine/05_world_objects_ai_systems.md).
// Requests come from the F10 menu and are carried out from the player update (the game thread), once per frame, so nothing is
// touched from the middle of a draw call.
#include "../base.h"
#include "../item_limits.h"
#include "../item_names.h"
#include "../sites.h"

namespace ufx {
namespace {

volatile int g_money = 0;      // 0 none, > 0 add that much, -1 the maximum
volatile int g_timeHour = -1;  // jump to this hour
volatile int g_weatherSet = 0; // 1 clear, 2 cloudy, 3 rain (applied), 4 give the weather back to the game
volatile int g_weatherInstant = 0;  // 1 = no 4 second fade (after a time jump)
volatile int g_ammo = 0;
volatile int g_toCamera = 0;

// Below the top of the user address space as the system reports it (UserAddressEnd): with the large address flag set by
// ufx_laa.exe heap blocks can lie above 0x80000000.
bool GoodPtr(u32 p) { return p >= 0x10000 && p < UserAddressEnd(); }

int DayPart(int hour) { return (hour >= 4 && hour <= 8) ? 0 : (hour >= 9 && hour <= 14) ? 1 : (hour >= 15 && hour <= 21) ? 2 : 3; }

typedef void(__attribute__((thiscall)) * SetTimeFn)(void* timer, int hour, int minute, int flag);
typedef void(__attribute__((thiscall)) * WeatherFn)(void* weather, int slot, int instant);

bool g_clockWasStopped = false;
u32 g_clockBits = 0;          // the game's own four clock bits, as they were when the stop began (plus any it set since)
float g_clockSpeedWritten = 0.f;  // the multiplier the speed cheat last wrote, 0 = the cheat is not active
float g_clockSpeedOrig = 1.f;     // the game's multiplier before the speed cheat changed it
int g_lastWeatherApplied = 0;
int g_weatherDay = -1, g_weatherPart = -1;  // the day and part of the day the chosen weather was applied in

// Puts the chosen weather (1 clear, 2 cloudy, 3 rain) in place. The engine's weather function keeps the override only while the day
// and the part of the day match the two fields it was set with, and it stores the SCRIPTED type in CWeather+0x1272 (only the look
// and the rain flag follow the override), so the type is set here too: the rest of the game then agrees with the picture.
void ApplyWeather(u8* game, u8* timer, u32 weatherAddr, int w, int instant) {
    int day = *(volatile int*)(timer + 8), part = DayPart(*(volatile int*)(timer + 0xC));
    *(volatile int*)(game + 0xCFF34) = day;
    *(volatile int*)(game + 0xCFF38) = part;
    *(volatile signed char*)(game + 0xCFF3C) = (signed char)(w - 1);  // 0 clear, 1 cloudy, 2 rain
    // slot -1 = "by the hour": the function then clears bit 2 of the weather flags first (with that bit set it returns at once and
    // the weather cannot change), picks the part of the day itself and applies the override above
    *(volatile u32*)(weatherAddr + 4) &= ~2u;
    ((WeatherFn)(uintptr_t)0x472D00)((void*)(uintptr_t)weatherAddr, -1, instant);
    *(volatile signed char*)(weatherAddr + 0x1272) = (signed char)(w - 1);
    g_lastWeatherApplied = w;
    g_weatherDay = day;
    g_weatherPart = part;
    Log("cheat: the weather is %s%s", w == 1 ? "clear" : w == 2 ? "cloudy" : "rain", instant ? " (at once)" : "");
}

}  // namespace

void CheatMoney(int amount) { g_money = amount; PhotoThaw(20); }
void CheatSetHour(int hour) { g_timeHour = hour; PhotoThaw(20); }
void CheatWeather(int type) { g_weatherSet = type; PhotoThaw(20); }
void CheatAmmo() { g_ammo = 1; PhotoThaw(20); }
// The spawner. Cars: the player's cars are numbered objects of the open world; FUN_005F1840 (thiscall on 0143C078h: number,
// place node or -1, reveal; ret 0Ch) loads and shows car N, as the scripts do for reward cars (8Fh/4Bh), and FUN_00541760
// (thiscall on the car: position[4], rotation[4]; ret 8) moves it. Items: FUN_005019B0 (thiscall on CGame: id, count, 1, 0, 0;
// ret 14h), the call behind the script command 84h in its silent form.
volatile int g_spawnCar = 0, g_giveItem = 0, g_giveCount = 0;
volatile int g_spawnCarWhere = 0;
float g_spawnCarPlace[4];
void CheatSpawnCarAt(int number, int where, const float place[4]) {
    for (int i = 0; i < 4; ++i) g_spawnCarPlace[i] = place ? place[i] : 0.f;
    g_spawnCarWhere = place || where != 3 ? where : 0;
    g_spawnCar = number;
    PhotoThaw(180);
}
void CheatSpawnCar(int number) { CheatSpawnCarAt(number, 0, nullptr); }
void CheatGiveItem(int id, int count) { g_giveCount = count; g_giveItem = id; PhotoThaw(20); }
constexpr u32 kCarShowFn = 0x5F1840, kCarMoveFn = 0x541760, kGiveItemFn = 0x5019B0, kWorldFactoryVa = 0x143C078;
typedef void*(__attribute__((thiscall)) * CarShowFn)(void* self, u32 number, int placeNode, int reveal);
typedef void(__attribute__((thiscall)) * CarMoveFn)(void* car, float* pos, float* rot);
typedef int(__attribute__((thiscall)) * GiveItemFn)(void* game, int id, int count, int toStorage, int a, int b);
inline float SinC(float x) { float r; __asm__("fsin" : "=t"(r) : "0"(x)); return r; }
inline float CosC(float x) { float r; __asm__("fcos" : "=t"(r) : "0"(x)); return r; }
void SpawnerTick(u8* game, u8* player) {
    static const u8 kShow[8] = {0x51, 0x56, 0x8B, 0xF1, 0x89, 0x74, 0x24, 0x04}, kMove[8] = {0x8B, 0x44, 0x24, 0x04, 0x56, 0x8B, 0xF1, 0x81},
                    kGive[8] = {0x83, 0xEC, 0x08, 0x53, 0x55, 0x56, 0x8B, 0xD9};
    const bool free = *(volatile u32*)(uintptr_t)kGameplayModeVa == 3 && !(*(volatile u32*)(uintptr_t)kEventModeVa & 0x80000000u);
    if (g_giveItem) {
        const int id = g_giveItem, count = g_giveCount;
        g_giveItem = 0;
        if (!free || !BytesMatch(kGiveItemFn, kGive, 8) || id < 1 || id > 0x16D || count < 1) {
            Log("spawner: item %d not given (needs free play; the function must be DP.exe 1.01b's)", id);
        } else {
            const int r = GiveItemNow(id, count);
            if (r == -2) ShowNotice(L"Гра не дала предмет", L"The game gave no item", L"Він уже є або для нього немає місця. Деталі в ufx.log.", L"York has it already or there is no room. See ufx.log.", 4);
            else if (r == -3) ShowNotice(L"Предмет пішов у сховище", L"The item went to the storage", L"В інвентарі немає місця.", L"There is no room in the inventory.", 3);
            else ShowNotice(L"Предмет видано", L"Item given", L"Дивіться інвентар.", L"See the inventory.", 2);
        }
    }
    if (g_spawnCar) {
        const int number = g_spawnCar;
        g_spawnCar = 0;
        if (!player || !free || !BytesMatch(kCarShowFn, kShow, 8) || !BytesMatch(kCarMoveFn, kMove, 8) || !(*(volatile u32*)(game + 0x8C5EC) & 2) ||
            *(volatile u32*)(game + 0x834FE0) != 0) {
            Log("spawner: car %d not put (needs free play outdoors in Greenvale)", number);
            ShowNotice(L"Машина: не зараз", L"Car: not now", L"Потрібна вільна гра надворі у Грінвейлі, не в будівлі й не в сцені.",
                       L"It needs free play outdoors in Greenvale, not in a building or a scene.", 4);
            return;
        }
        u8* car = (u8*)((CarShowFn)(uintptr_t)kCarShowFn)((void*)(uintptr_t)kWorldFactoryVa, (u32)number, -1, 1);
        if (!car || (*(volatile u32*)(car + 0xDC) & 0x60)) {
            Log("spawner: the game gave no car %d (%08X)", number, (u32)(uintptr_t)car);
            ShowNotice(L"Машина не з'явилась", L"The car did not appear", L"Гра не дала цієї машини. Деталі в ufx.log.", L"The game gave no such car. See ufx.log.", 4);
            return;
        }
        const float yaw = *(volatile float*)(player + 0x7C);
        // in front of York (the cheat page), behind him or well ahead of him (a quest step: a character stands 8 units in front), or
        // at a place of the quest's own
        const int where = g_spawnCarWhere;
        const float away = where == 1 ? -28.f : where == 2 ? 40.f : 16.f;
        float pos[4] = {*(volatile float*)(player + 0x58) + SinC(yaw) * away, *(volatile float*)(player + 0x5C) + 1.f,
                        *(volatile float*)(player + 0x60) + CosC(yaw) * away, 1.f};
        float rot[4] = {0.f, yaw + 1.5707963f, 0.f, 0.f};  // side on to York
        if (where == 3) {
            pos[0] = g_spawnCarPlace[0];
            pos[1] = g_spawnCarPlace[1] + 1.f;
            pos[2] = g_spawnCarPlace[2];
            rot[1] = g_spawnCarPlace[3];
        }
        ((CarMoveFn)(uintptr_t)kCarMoveFn)(car, pos, rot);
        Log("spawner: car %d (object %08X, subtype %d) put at %d %d %d", number, (u32)(uintptr_t)car, (int)*(volatile signed char*)(car + 0x474),
            (int)pos[0], (int)pos[1], (int)pos[2]);
    }
}
void CheatYorkToCamera() { g_toCamera = 1; PhotoThaw(20); }
volatile int g_yorkTo = 0;
float g_yorkToPlace[4];
// York put at a place (the quest runner's "teleport"): the position and heading written into the player object, as the move
// to the free camera does. No level change: a place in another zone is not reached this way.
void CheatYorkTo(const float place[4]) {
    for (int i = 0; i < 4; ++i) g_yorkToPlace[i] = place[i];
    g_yorkTo = 1;
    PhotoThaw(20);
}

// Runs once per frame from the player update (esi = the player object).
void CheatsTick(void* player) {
    FlyHold(player);
    u8* game = *(u8**)(uintptr_t)kGameManagerPtrVa;
    if (!game || !GoodPtr((u32)(uintptr_t)game)) return;
    u32 timerAddr = *(volatile u32*)(uintptr_t)0xBE1EA0;
    u8* timer = GoodPtr(timerAddr) ? (u8*)(uintptr_t)timerAddr : nullptr;
    int kind = (signed char)game[0x8CA12];

    // health: the current value is brought back to the maximum every frame
    if (g_cfg.cheatGod && kind >= 0 && kind <= 4) {
        volatile float* hp = (volatile float*)(game + 0xCEB88 + kind * 4);
        float mx = *(volatile float*)(game + 0xCEB74 + kind * 4);
        if (mx > 0.f && mx < 100000.f) *hp = mx;
    }

    // the clock: stopped by clearing its four enabling flags, speed is a float multiplier
    if (timer) {
        volatile u32* flags = (volatile u32*)(timer + 4);
        if (g_cfg.cheatClockStop) {
            // The game sets and clears these bits one by one (events, indoors), and the clock runs only with all four set. So
            // the stop remembers the game's own bits and gives back exactly those, not all four. A bit the game sets while the
            // clock is stopped shows up here between two frames and is added. A bit it clears in that time cannot be seen
            // (it is already clear) and comes back as it was before the stop.
            if (!g_clockWasStopped) g_clockBits = *flags & 0xFu;
            else g_clockBits |= *flags & 0xFu;
            *flags &= ~0xFu;
            g_clockWasStopped = true;
        } else if (g_clockWasStopped) {
            *flags = (*flags & ~0xFu) | g_clockBits;
            g_clockWasStopped = false;
        }
        volatile float* speed = (volatile float*)(timer + 0x1C);
        // FUN_00470590 (a script waiting for a time) sets bit 10h and runs the clock fast (CTimer+1Ch) until the time is reached:
        // leave the rate alone then, or that wait takes as long as at the cheat's rate
        const bool scriptedFast = (*flags & 0x10u) != 0;
        if (g_cfg.cheatClockSpeed != 10 && !scriptedFast) {
            if (g_clockSpeedWritten == 0.f) g_clockSpeedOrig = *speed;  // the game's own rate (scripts set it), to give back
            g_clockSpeedWritten = (float)g_cfg.cheatClockSpeed / 10.0f;
            *speed = g_clockSpeedWritten;
        } else if (g_clockSpeedWritten != 0.f) {
            // back to 10: give the game its rate again, unless the game has written its own since (then that one stays)
            if (*speed == g_clockSpeedWritten) *speed = g_clockSpeedOrig > 0.f && g_clockSpeedOrig < 100.f ? g_clockSpeedOrig : 1.0f;
            g_clockSpeedWritten = 0.f;
        } else if (scriptedFast && g_clockSpeedWritten != 0.f) {
            g_clockSpeedWritten = 0.f;  // the script's rate is in place; take ours up again after it (from the rate then)
        }
    }

    if (g_money) {
        volatile float* money = (volatile float*)(game + 0x8C5F0);
        // "the maximum" goes up by 10000 a frame, like the +10000 button that is known to be safe: one jump of 80000 hung the game
        // (the number display of the game is the suspect), several steps look the same to it as a few purchases
        int step = g_money < 0 ? 10000 : g_money;
        float target = g_money < 0 ? 99999.0f : *money + (float)g_money;
        if (target > 99999.0f) target = 99999.0f;
        float v = *money + (float)step;
        if (v > target) v = target;
        *money = v;
        if (v >= target) {
            Log("cheat: money is now %d", (int)v);
            g_money = 0;
        } else {
            g_money = -1;  // keep going next frame
        }
    }

    if (g_timeHour >= 0 && timer) {
        int h = g_timeHour;
        g_timeHour = -1;
        if (h <= 23) {
            // SetTimeOfDay (470950h) passes its time on with "may roll over" set (470320h), which adds a day when the new time is
            // earlier than the current one: an earlier hour is the next day
            int nowSecs = (*(volatile int*)(timer + 0xC) * 60 + *(volatile int*)(timer + 0x10)) * 60 + *(volatile int*)(timer + 0x14);
            bool nextDay = h * 3600 < nowSecs;
            // flag 1 as the game's sleep does (004F364A push 1/-1/-1): only then SetTimeOfDay calls 0043F890, which starts the
            // town's day/night switch (events 3Fh/40h) when the new time crosses 0:00 or 6:00
            ((SetTimeFn)(uintptr_t)0x470950)(timer, h, 0, 1);
            Log("cheat: the time is set to %d:00%s", h, nextDay ? " of the next day" : "");
            // SetTimeOfDay drops the weather override (day or part of the day changed) and puts the scripted weather in at once:
            // put the chosen weather back, also at once
            if (g_lastWeatherApplied && !g_weatherSet) { g_weatherSet = g_lastWeatherApplied; g_weatherInstant = 1; }
        }
    }

    if (g_weatherSet && timer) {
        u32 weatherAddr = *(volatile u32*)(uintptr_t)0xBDA0F4;
        int w = g_weatherSet, instant = g_weatherInstant;
        g_weatherSet = 0;
        g_weatherInstant = 0;
        if (w == 4) {
            *(volatile signed char*)(game + 0xCFF3C) = -1;  // the game picks the weather again
            g_lastWeatherApplied = 0;
            if (GoodPtr(weatherAddr)) {
                *(volatile u32*)(weatherAddr + 4) &= ~2u;
                ((WeatherFn)(uintptr_t)0x472D00)((void*)(uintptr_t)weatherAddr, -1, 0);  // back to the scripted weather of the hour
            }
            Log("cheat: the weather is the game's again");
        } else if (GoodPtr(weatherAddr)) {
            ApplyWeather(game, timer, weatherAddr, w, instant);
        }
    } else if (g_lastWeatherApplied && timer) {
        // keep the override alive: the engine drops it when the day or the part of the day changes (and then puts the scripted
        // weather in), so when it changed since the last frame, apply the chosen weather again
        int day = *(volatile int*)(timer + 8), part = DayPart(*(volatile int*)(timer + 0xC));
        u32 weatherAddr = *(volatile u32*)(uintptr_t)0xBDA0F4;
        if ((day != g_weatherDay || part != g_weatherPart) && GoodPtr(weatherAddr))
            ApplyWeather(game, timer, weatherAddr, g_lastWeatherApplied, 0);
        else {
            *(volatile int*)(game + 0xCFF34) = day;
            *(volatile int*)(game + 0xCFF38) = part;
            *(volatile signed char*)(game + 0xCFF3C) = (signed char)(g_lastWeatherApplied - 1);
        }
    }

    SpawnerTick(game, (u8*)player);
    if (g_ammo) {
        g_ammo = 0;
        int id = *(volatile int*)(game + 0x98E48);
        u32 itemsAddr = *(volatile u32*)(game + 0x835000);
        // the magazine array at CGame+98E50h has 43h entries (FUN_00452bc0 fills 43h, FUN_004fd8a0 clamps above 42h)
        if (id > 0 && id < 0x43 && GoodPtr(itemsAddr)) {
            int mx = *(volatile short*)(uintptr_t)(itemsAddr + (u32)id * 0xDC + 0x3E);
            if (mx > 0 && mx <= 9999) {
                *(volatile float*)(game + 0x98E50 + id * 4) = (float)mx;
                Log("cheat: ammunition of item %d filled to %d", id, mx);
            } else {
                Log("cheat: item %d has no ammunition limit I can read (%d)", id, mx);
            }
        } else {
            Log("cheat: no weapon in hand");
        }
    }

    if (g_yorkTo && player) {
        g_yorkTo = 0;
        u8* p = (u8*)player;
        for (int i = 0; i < 3; ++i) *(volatile float*)(p + 0x58 + 4 * i) = g_yorkToPlace[i];
        *(volatile float*)(p + 0x7C) = g_yorkToPlace[3];
        Log("cheat: the player put at %d %d %d", (int)g_yorkToPlace[0], (int)g_yorkToPlace[1], (int)g_yorkToPlace[2]);
    }
    if (g_toCamera) {
        g_toCamera = 0;
        if (g_cfg.photoActive && player) {
            volatile float* eye = (volatile float*)(uintptr_t)0xBD964C;
            u8* p = (u8*)player;
            *(volatile float*)(p + 0x58) = eye[0];
            *(volatile float*)(p + 0x5C) = eye[1];
            *(volatile float*)(p + 0x60) = eye[2];
            Log("cheat: the player moved to the camera");
        } else {
            Log("cheat: fly the free camera first, then move the player to it");
        }
    }
}

// The friend's car (net_coop.cpp). One of the game's numbered cars is taken as the spawner takes one (FUN_005F1840) and put where
// the friend's game says, every update.
//   FUN_00541760 only writes the place and the three angles (pitch, heading, roll) and rebuilds the picture: it touches no physics
//   and raises car+434h bit 20000000h, which the car's own update answers by tearing its actors down. So that bit is cleared again.
//   What can be bumped into, for a parked car, is a set of fixed boxes the game builds once (FUN_0054F000) when York comes within
//   100 units, and never moves: the picture drove away from them. With NetCarCollision the boxes are built as moving ones instead
//   (the creation flags 44h -> 42h for that one call, the byte at 0054F2A8), and FUN_00719990 ties them to the car's bones: the
//   engine then carries them along every frame (FUN_007178C0), tilt included. car+12F0h = 1 keeps the game's own box code off.
constexpr u32 kCarBoxesFn = 0x54F000, kCarBoxFlagsVa = 0x54F2A8, kBonesFollowFn = 0x719990, kActorsFreeFn = 0x718F00, kNetObjLookupFn = 0x6C5FD0,
              kNetObjMgrVa = 0xBD7670, kNetCarVtable = 0x773F74;
typedef void(__attribute__((thiscall)) * CarBoxesFn)(void* car);
typedef void(__attribute__((thiscall)) * BonesFollowFn)(void* car, int bone, int on);
typedef void*(__attribute__((thiscall)) * NetObjLookupFn)(void* mgr, u32 handle);
u8* g_netCar = nullptr;
u32 g_netCarHandle = 0;
int g_netCarNumber = 0;
bool g_netCarBoxes = false;

static bool NetCarAlive() {
    if (!g_netCar) return false;
    void* mgr = *(void* volatile*)(uintptr_t)kNetObjMgrVa;
    if (mgr && ((NetObjLookupFn)(uintptr_t)kNetObjLookupFn)(mgr, g_netCarHandle) == g_netCar && *(volatile u32*)g_netCar == kNetCarVtable) return true;
    g_netCar = nullptr;
    g_netCarBoxes = false;
    return false;
}
static bool NetCarBoxFnsOk() {
    static const u8 kBoxes[8] = {0xB8, 0x34, 0x1C, 0x00, 0x00, 0xE8, 0xE6, 0x1F}, kFollow[8] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0x89, 0x4D},
                    kFree[8] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x44, 0x89, 0x4D}, kFlags[7] = {0xC7, 0x45, 0x10, 0x44, 0x00, 0x00, 0x00};
    return BytesMatch(kCarBoxesFn, kBoxes, 8) && BytesMatch(kBonesFollowFn, kFollow, 8) && BytesMatch(kActorsFreeFn, kFree, 8) && BytesMatch(kCarBoxFlagsVa - 3, kFlags, 7);
}
static void NetCarBoxesOff(u8* car) {
    if (g_netCarBoxes && *(void* volatile*)(car + 0x264)) ((CarBoxesFn)(uintptr_t)kActorsFreeFn)(car);
    g_netCarBoxes = false;
}
// The friend left the car (or the place): the car stays where it is, as a parked one of the game's own again.
void NetCarRelease(const char* why) {
    if (!NetCarAlive()) return;
    NetCarBoxesOff(g_netCar);
    *(volatile u8*)(g_netCar + 0x12F0) = 0;
    Log("net partner: the friend's car is let go (%s)", why);
    g_netCar = nullptr;
}
// Returns the car object, nullptr when it could not be shown. rot = pitch, heading, roll; york = this York's place.
u8* NetCarAt(int number, const float pos[3], const float rot[3], const void* mine, const float york[3]) {
    static const u8 kShow[8] = {0x51, 0x56, 0x8B, 0xF1, 0x89, 0x74, 0x24, 0x04}, kMove[8] = {0x8B, 0x44, 0x24, 0x04, 0x56, 0x8B, 0xF1, 0x81};
    u8* game = *(u8* volatile*)(uintptr_t)kGameManagerPtrVa;
    const bool free = *(volatile u32*)(uintptr_t)kGameplayModeVa == 3 && !(*(volatile u32*)(uintptr_t)kEventModeVa & 0x80000000u);
    if (!game || !free || !BytesMatch(kCarShowFn, kShow, 8) || !BytesMatch(kCarMoveFn, kMove, 8) || !(*(volatile u32*)(game + 0x8C5EC) & 2) ||
        *(volatile u32*)(game + 0x834FE0) != 0) {
        if (g_netCar) NetCarRelease("not free play outdoors in Greenvale");
        return nullptr;
    }
    static DWORD asked = 0;
    if (!NetCarAlive() || g_netCarNumber != number) {
        if (GetTickCount() - asked < 1000 && g_netCarNumber == number) return nullptr;  // the game gave none a moment ago
        asked = GetTickCount();
        if (g_netCar) NetCarRelease("another car");
        g_netCarNumber = number;
        u8* got = (u8*)((CarShowFn)(uintptr_t)kCarShowFn)((void*)(uintptr_t)kWorldFactoryVa, (u32)number, -1, 1);
        if (got && got == mine) {  // the very car this York sits in: another one stands in
            const int other = number == 7 ? 8 : 7;
            Log("net partner: car %d is the one York sits in, car %d stands in for the friend's", number, other);
            got = (u8*)((CarShowFn)(uintptr_t)kCarShowFn)((void*)(uintptr_t)kWorldFactoryVa, (u32)other, -1, 1);
            if (got == mine) got = nullptr;
        }
        if (!got || *(volatile u32*)got != kNetCarVtable || (*(volatile u32*)(got + 0xDC) & 0x60)) {
            Log("net partner: the game gave no car %d for the friend (%08X)", number, (u32)(uintptr_t)got);
            return nullptr;
        }
        g_netCar = got;
        g_netCarHandle = *(volatile u32*)(got + 0x3C);
        g_netCarBoxes = false;
        Log("net partner: the friend's car is car %d (object %08X, subtype %d)", number, (u32)(uintptr_t)got, (int)*(volatile signed char*)(got + 0x474));
    }
    u8* car = g_netCar;
    if (car == mine) return nullptr;
    float p[4] = {pos[0], pos[1], pos[2], 1.f};
    float r[4] = {rot[0], rot[1], rot[2], 0.f};
    ((CarMoveFn)(uintptr_t)kCarMoveFn)(car, p, r);
    *(volatile u32*)(car + 0x434) &= ~0x20000000u;  // no teardown: nothing of the game's placed it
    if (g_cfg.netCarCollision && NetCarBoxFnsOk()) {
        *(volatile u8*)(car + 0x12F0) = 1;
        const float dx = pos[0] - york[0], dz = pos[2] - york[2], d2 = dx * dx + dz * dz;
        if (g_netCarBoxes && (d2 > 110.f * 110.f || !*(void* volatile*)(car + 0x264))) NetCarBoxesOff(car);
        if (!g_netCarBoxes && d2 < 90.f * 90.f) {
            if (*(void* volatile*)(car + 0x264)) ((CarBoxesFn)(uintptr_t)kActorsFreeFn)(car);  // the game's fixed ones, left where the car stood
            const u8 moving = 0x42, fixed = 0x44;
            if (WriteCode(kCarBoxFlagsVa, &moving, 1)) {
                ((CarBoxesFn)(uintptr_t)kCarBoxesFn)(car);
                WriteCode(kCarBoxFlagsVa, &fixed, 1);
                if (*(void* volatile*)(car + 0x264)) {
                    ((BonesFollowFn)(uintptr_t)kBonesFollowFn)(car, -1, 1);
                    g_netCarBoxes = true;
                    Log("net partner: the friend's car can be bumped into (moving boxes tied to its bones)");
                } else {
                    Log("net partner: the game built no boxes for the friend's car (it stays a picture)");
                    asked = GetTickCount();
                }
            }
        }
    }
    return car;
}

int GiveItemNow(int id, int count) {
    static const u8 kGive[8] = {0x83, 0xEC, 0x08, 0x53, 0x55, 0x56, 0x8B, 0xD9};
    u8* game = *(u8* volatile*)(uintptr_t)kGameManagerPtrVa;
    if (!game || !BytesMatch(kGiveItemFn, kGive, 8) || id < 1 || id > 0x16D || count < 1) return -1;
    // what York carries (+98270h) and what lies in the storage (+97698h), by item id: read around the call to see what it did
    volatile int* carried = (volatile int*)(game + 0x98270 + id * 4);
    volatile int* stored = (volatile int*)(game + 0x97698 + id * 4);
    if (id <= 66 && kItemMax[id] == 0) {  // a weapon the game's item table lets York own none of: its own call would answer -2
        Log("give: item %d (%s) not given: the game's item table lets York own none of it (see the <127> one of the same weapon)", id, kItemNames[id]);
        return -2;
    }
    const int c0 = *carried, s0 = *stored;
    int r = ((GiveItemFn)(uintptr_t)kGiveItemFn)(game, id, count, 1, 0, 0);
    Log("give: item %d x %d, the game answered %d; carried %d -> %d, stored %d -> %d", id, count, r, c0, *carried, s0, *stored);
    if (*carried == c0 && *stored == s0 && r != -3) {
        r = -2;  // nothing was added: York has it already, or there is no room for more
    }
    return r;
}

}  // namespace ufx

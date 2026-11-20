// Cheats and world control: money, health, the clock, the weather, ammunition, "York to the camera".
//
// Every field and function used here was found in the static analysis of DP.exe 1.01b (docs/engine/05_world_objects_ai_systems.md).
// Requests come from the F10 menu and are carried out from the player update (the game thread), once per frame, so nothing is
// touched from the middle of a draw call.
#include "../base.h"
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
void CheatYorkToCamera() { g_toCamera = 1; PhotoThaw(20); }

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

}  // namespace ufx

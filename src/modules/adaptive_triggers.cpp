// Adaptive trigger effects by weapon. The class of the weapon in York's hands is visible in the player state while he
// aims (the states are those of the weapon classes of ITEM.PRM): the effect of the fire trigger follows it. The triggers
// carry aim and fire only with the trigger layout (TriggerFire, combat_strafe.cpp); with the game's own layout they get no
// weapon effect. With SwapTriggers the fire trigger is the left one, and the effects change sides with it.
#include "../base.h"
#include "../dsreport.h"

namespace ufx {

bool DsOutputReady();
void DsSetEffects(bool useRight, const TriggerEffect& right, bool useLeft, const TriggerEffect& left);
bool PlayerStateNow(u32* state);
int WeaponClassNow();
bool TriggerLayoutActive();

namespace {

int Scaled(int base) {
    int v = (base * g_cfg.adaptiveStrength + 50) / 100;
    if (g_cfg.adaptiveStrength > 0 && v < 1) v = 1;
    return v;
}

}  // namespace

// Called once per frame from the Present hook.
void AdaptiveTriggerFrame() {
    // The test is a menu item: it ends when the menu closes, so a forgotten test never replaces the weapon effects in the game.
    if (g_cfg.triggerTest > 0 && !ui::IsOpen()) g_cfg.triggerTest = 0;
    if (!DsOutputReady()) return;
    TriggerEffect right = EffectOff(), left = EffectOff();
    bool useRight = true, useLeft = true;  // an "off" effect is sent as well, so that a stale effect never stays
    const int test = g_cfg.triggerTest;
    if (test > 0) {  // from the menu: feel the effect without the game, on the physical right trigger, also with AdaptiveTriggers off
        switch (test) {
            case 1: right = EffectWeapon(4, 6, Scaled(6)); break;
            case 2: right = EffectWeapon(3, 7, Scaled(8)); break;
            case 3: right = EffectVibration(3, Scaled(5), 30); break;
            case 4: right = EffectFeedback(2, Scaled(4)); break;
            case 5: right = EffectVibration(2, Scaled(3), 10); break;
            case 6: right = EffectWeapon(4, 6, Scaled(6)); left = EffectFeedback(2, Scaled(4)); break;
        }
        DsSetEffects(useRight, right, useLeft, left);
        return;
    }
    if (!g_cfg.adaptiveTriggers) {  // not wanted: leave the triggers to whoever else controls them
        DsSetEffects(false, right, false, left);
        return;
    }
    if (g_cfg.adaptiveStrength > 0) {
        if (!ui::IsOpen() && *(volatile u8*)(uintptr_t)0x014810F0) {
            HWND w = GameWindowHandle();
            u32 state = 0;
            if (w && GetForegroundWindow() == w && PlayerStateNow(&state)) {
                bool ranged = true;
                switch (state) {
                    case 0x1A: right = EffectWeapon(4, 6, Scaled(5)); break;           // 9mm pistols, dart gun
                    case 0x1C: right = EffectWeapon(3, 6, Scaled(8)); break;           // .357 Magnum
                    case 0x1E: right = EffectVibration(3, Scaled(5), 30); break;       // 10mm SMG
                    case 0x20: right = EffectWeapon(3, 7, Scaled(8)); break;           // 12 Ga shotgun
                    case 0x22: right = EffectVibration(3, Scaled(4), 18); break;       // 5.56 assault rifle
                    case 0x2A: right = EffectWeapon(2, 8, Scaled(8)); break;           // RPG
                    case 0x2B: right = EffectVibration(2, Scaled(2), 8); break;        // flamethrower
                    case 0x26: right = EffectVibration(1, Scaled(3), 40); break;       // grass cutter
                    default: ranged = false; break;                                    // also driving (87h): no effect in a car
                }
                if (!ranged && state >= 0x14 && state <= 0x2B) {  // another state of a weapon class (reload, ready, fire): by the class of the item
                    int cls = WeaponClassNow();
                    switch (cls) {
                        case 4: right = EffectWeapon(4, 6, Scaled(5)); ranged = true; break;
                        case 5: right = EffectVibration(3, Scaled(5), 30); ranged = true; break;
                        case 6: right = EffectWeapon(3, 7, Scaled(8)); ranged = true; break;
                        case 9: right = EffectWeapon(2, 8, Scaled(8)); ranged = true; break;
                        case 10: right = EffectVibration(2, Scaled(2), 8); ranged = true; break;
                        case 11: right = EffectVibration(1, Scaled(3), 40); ranged = true; break;
                        default: break;
                    }
                }
                if (!TriggerLayoutActive()) {  // fire is on R1, aim on L1: the triggers do not shoot
                    ranged = false;
                    right = EffectOff();
                }
                static u32 lastState = 0xFFFFFFFFu;
                static int lastBudget = 200;
                if (state != lastState && lastBudget > 0) {  // what the pad was told: the labelled table the mapping needs
                    lastState = state;
                    --lastBudget;
                    Log("adaptive: player state %u, weapon class %d, %s", state, WeaponClassNow(), ranged ? "weapon effect" : "no effect");
                }
                if (ranged && g_cfg.adaptiveAim) left = EffectFeedback(3, Scaled(2));  // a light resistance on the aim trigger
            }
        }
    }
    if (g_cfg.swapTriggers) {  // the pad's left trigger fires: the effects follow the inputs (DualSenseToJoy swaps them the same way)
        TriggerEffect t = right;
        right = left;
        left = t;
    }
    DsSetEffects(useRight, right, useLeft, left);
}

}  // namespace ufx

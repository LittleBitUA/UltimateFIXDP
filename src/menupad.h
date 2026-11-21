// The F10 menu on a gamepad: one frame of pad data in the Xbox 360 layout of WinMM in, the menu's logical buttons out, with
// edge detection and hold-to-repeat. No Windows dependency, so the host can test it (tests/test_menupad.cpp); menu.cpp reads
// the pad and turns these buttons into the menu's actions.
//
// The data is what the game gets for pad 0 (dualsense.cpp, xinput_input.cpp, sony_layout.cpp all produce it): X/Y left stick
// 0..65535 with up small, Z both triggers on one axis (LT above the centre, RT below), buttons A B X Y LB RB Back Start LS RS
// = 0..9 and L2/R2 as 10/11 where the source sets them, the hat in hundredths of a degree (0xFFFF = centred).
#pragma once

namespace ufx {

// The logical buttons of the menu.
enum : unsigned {
    kPadUp = 1u << 0,
    kPadDown = 1u << 1,
    kPadLeft = 1u << 2,
    kPadRight = 1u << 3,
    kPadAccept = 1u << 4,       // A / Cross: Enter
    kPadBack = 1u << 5,         // B / Circle: Esc
    kPadPrevSection = 1u << 6,  // LB / L1: Shift+Tab
    kPadNextSection = 1u << 7,  // RB / R1: Tab
    kPadPageUp = 1u << 8,       // LT / L2: a screen of rows up
    kPadPageDown = 1u << 9,     // RT / R2: a screen of rows down
    kPadReset = 1u << 10,       // Y / Triangle: Backspace
    kPadOpen = 1u << 11,        // View + Menu (Create + Options) held together: opens and closes the menu
};
constexpr int kPadLogicalCount = 12;
// Held, these come again: first after 350 ms, then every 90 ms. The others act once per press (a held A must not flip a switch
// ten times a second).
constexpr unsigned kPadRepeating = kPadUp | kPadDown | kPadLeft | kPadRight | kPadPageUp | kPadPageDown;
constexpr unsigned kPadFirstRepeatMs = 350, kPadRepeatMs = 90;

// Bits of the Xbox layout's buttons.
constexpr unsigned kXbA = 1u << 0, kXbB = 1u << 1, kXbY = 1u << 3, kXbLB = 1u << 4, kXbRB = 1u << 5, kXbBack = 1u << 6,
                   kXbStart = 1u << 7, kXbLT = 1u << 10, kXbRT = 1u << 11;
constexpr unsigned kPadOpenPair = kXbBack | kXbStart;

// The left stick counts as a direction past half of its travel and lets go below 0.375 of it: a stick resting near the
// threshold does not chatter. Only its larger axis counts (a diagonal would move the row and change its value at once), and the
// direction it took is kept while it stays past the lower mark, so a stick rolling along the edge does not flip between two.
constexpr int kPadStickOn = 16384, kPadStickOff = 12288;
// A trigger on the shared axis counts past about half of its travel.
constexpr int kPadTriggerOn = 16000;

struct MenuPadRaw {
    bool valid;          // a pad answered this frame
    unsigned buttons;    // the Xbox layout bits above
    unsigned pov;        // hundredths of a degree, 0xFFFF (or anything from 36000 up) = centred
    unsigned x, y;       // left stick, 0..65535, centre 32767, up small
    unsigned z;          // the shared trigger axis
    bool zTriggers;      // z really is the trigger axis (not the right stick of a DirectInput pad)
};

struct MenuPad {
    unsigned down = 0;      // the logical buttons held in the last frame
    unsigned blocked = 0;   // held when the pad was (re)started: they count only once they were let go
    bool primed = false;    // false = the next frame only learns what is held
    unsigned stick = 0;     // the direction the stick holds now (one of kPadUp..kPadRight or 0)
    unsigned nextRepeat[kPadLogicalCount] = {};  // tick of the next repeat of a held button
    int repeats[kPadLogicalCount] = {};          // repeats so far in this hold (the sliders speed up with it)
};

struct MenuPadFrame {
    unsigned down;     // held now
    unsigned pressed;  // went down this frame, or repeated
    unsigned repeat;   // the part of `pressed` that is a repeat of a held button
};

// The next press is a fresh one: everything held now is ignored until it is let go (the menu just opened with the pad's
// own pair, the game window got the focus back with A held ...).
inline void MenuPadRestart(MenuPad& p) { p.primed = false; }

inline int MenuPadBit(unsigned bit) {
    int i = 0;
    while (i < 31 && !(bit & (1u << i))) ++i;
    return i;
}

// How many repeats the hold of `bit` has had (0 = the first press or not held).
inline int MenuPadRepeats(const MenuPad& p, unsigned bit) {
    const int i = MenuPadBit(bit);
    return i < kPadLogicalCount ? p.repeats[i] : 0;
}

// The stick's direction under the hysteresis above.
inline unsigned MenuPadStick(unsigned held, unsigned x, unsigned y) {
    const int dx = (int)x - 32767, dy = (int)y - 32767;
    const int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (held == kPadLeft && dx <= -kPadStickOff) return held;
    if (held == kPadRight && dx >= kPadStickOff) return held;
    if (held == kPadUp && dy <= -kPadStickOff) return held;
    if (held == kPadDown && dy >= kPadStickOff) return held;
    if (ax >= ay && ax >= kPadStickOn) return dx < 0 ? kPadLeft : kPadRight;
    if (ay > ax && ay >= kPadStickOn) return dy < 0 ? kPadUp : kPadDown;
    return 0;
}

// The hat's directions: a diagonal holds both of its neighbours (MenuPadUpdate then keeps one of them).
inline unsigned MenuPadHat(unsigned pov) {
    if (pov >= 36000) return 0;
    unsigned d = 0;
    if (pov >= 31500 || pov <= 4500) d |= kPadUp;
    if (pov >= 4500 && pov <= 13500) d |= kPadRight;
    if (pov >= 13500 && pov <= 22500) d |= kPadDown;
    if (pov >= 22500 && pov <= 31500) d |= kPadLeft;
    return d;
}

// One frame: `now` is a millisecond tick (GetTickCount; it may wrap).
inline MenuPadFrame MenuPadUpdate(MenuPad& p, const MenuPadRaw& raw, unsigned now) {
    unsigned d = 0;
    if (raw.valid) {
        p.stick = MenuPadStick(p.stick, raw.x, raw.y);
        unsigned hat = MenuPadHat(raw.pov);
        // A diagonal keeps the axis already held: a thumb rolling off Right onto up-right must not also move the row (the
        // held Right would then change the new one); a fresh diagonal counts as up / down.
        if ((hat & (kPadUp | kPadDown)) && (hat & (kPadLeft | kPadRight)))
            hat &= (p.down & (kPadLeft | kPadRight)) ? (kPadLeft | kPadRight) : (kPadUp | kPadDown);
        d |= p.stick | hat;
        const unsigned b = raw.buttons;
        if ((b & kPadOpenPair) == kPadOpenPair) d |= kPadOpen;
        if (b & kXbA) d |= kPadAccept;
        if (b & kXbB) d |= kPadBack;
        if (b & kXbY) d |= kPadReset;
        if (b & kXbLB) d |= kPadPrevSection;
        if (b & kXbRB) d |= kPadNextSection;
        const int z = (int)raw.z - 32767;
        if ((b & kXbLT) || (raw.zTriggers && z >= kPadTriggerOn)) d |= kPadPageUp;
        if ((b & kXbRT) || (raw.zTriggers && z <= -kPadTriggerOn)) d |= kPadPageDown;
    } else {
        p.stick = 0;
    }
    MenuPadFrame f = {d, 0, 0};
    if (!p.primed) {
        p.primed = true;
        p.blocked = d;
        p.down = d;
        for (int i = 0; i < kPadLogicalCount; ++i) p.repeats[i] = 0;
        f.down = 0;
        return f;
    }
    p.blocked &= d;  // a let-go button is free again
    const unsigned live = d & ~p.blocked;
    for (int i = 0; i < kPadLogicalCount; ++i) {
        const unsigned bit = 1u << i;
        if (!(live & bit)) {
            p.repeats[i] = 0;
            continue;
        }
        if (!(p.down & bit)) {
            f.pressed |= bit;
            p.nextRepeat[i] = now + kPadFirstRepeatMs;
            p.repeats[i] = 0;
        } else if ((kPadRepeating & bit) && (int)(now - p.nextRepeat[i]) >= 0) {
            f.pressed |= bit;
            f.repeat |= bit;
            // a slow frame gives one repeat, not a burst to catch up
            p.nextRepeat[i] = (int)(now - p.nextRepeat[i]) > (int)kPadRepeatMs ? now + kPadRepeatMs : p.nextRepeat[i] + kPadRepeatMs;
            ++p.repeats[i];
        }
    }
    p.down = d;
    f.down = live;
    return f;
}

// How many steps a slider moves per repeat: one for about the first second of a hold, then four, then ten after about 2.5 s.
inline int MenuPadStepScale(int repeats) { return repeats < 8 ? 1 : repeats < 24 ? 4 : 10; }

}  // namespace ufx

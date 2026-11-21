// The F10 menu's pad reader (src/menupad.h): edges, hold-to-repeat, the stick's hysteresis, the hat, the triggers, the pair
// that opens the menu and the buttons held when the reader starts.
#include <stdio.h>
#include "menupad.h"
using namespace ufx;

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); ++g_fail; } } while (0)

static MenuPadRaw Idle() {
    MenuPadRaw r = {};
    r.valid = true;
    r.pov = 0xFFFF;
    r.x = r.y = r.z = 32767;
    r.zTriggers = true;
    return r;
}

int main() {
    // The first frame only learns; a button held then counts once it was let go.
    {
        MenuPad p;
        MenuPadRaw r = Idle();
        r.buttons = kXbA;
        MenuPadFrame f = MenuPadUpdate(p, r, 1000);
        CHECK(f.pressed == 0 && f.down == 0, "a button held at the start is not a press (%x)", f.pressed);
        f = MenuPadUpdate(p, r, 1016);
        CHECK(f.pressed == 0, "still held: no press");
        r.buttons = 0;
        MenuPadUpdate(p, r, 1032);
        r.buttons = kXbA;
        f = MenuPadUpdate(p, r, 1048);
        CHECK(f.pressed == kPadAccept, "pressed again after the release (%x)", f.pressed);
        f = MenuPadUpdate(p, r, 3000);
        CHECK(f.pressed == 0, "Accept does not repeat (%x)", f.pressed);
    }
    // Repeat: first after 350 ms, then every 90 ms, one per frame however slow the frame.
    {
        MenuPad p;
        MenuPadRaw r = Idle();
        MenuPadUpdate(p, r, 0);
        r.pov = 18000;
        MenuPadFrame f = MenuPadUpdate(p, r, 10);
        CHECK(f.pressed == kPadDown && !f.repeat, "hat down presses (%x)", f.pressed);
        int presses = 1;
        unsigned firstRepeat = 0;
        for (unsigned t = 26; t <= 10 + 350 + 90 * 10; t += 16) {
            f = MenuPadUpdate(p, r, t);
            if (f.pressed & kPadDown) {
                ++presses;
                if (!firstRepeat) firstRepeat = t;
            }
        }
        CHECK(firstRepeat >= 360 && firstRepeat < 376, "first repeat after 350 ms (%u)", firstRepeat);
        CHECK(presses >= 10 && presses <= 12, "about every 90 ms afterwards (%d presses)", presses);
        CHECK(MenuPadRepeats(p, kPadDown) == presses - 1, "repeats counted (%d)", MenuPadRepeats(p, kPadDown));
        f = MenuPadUpdate(p, r, 5000);  // a frame of four seconds
        CHECK(f.pressed == kPadDown, "a slow frame repeats once");
        f = MenuPadUpdate(p, r, 5010);
        CHECK(f.pressed == 0, "and does not catch up with a burst");
        r.pov = 0xFFFF;
        MenuPadUpdate(p, r, 5020);
        CHECK(MenuPadRepeats(p, kPadDown) == 0, "the count ends with the hold");
    }
    // Repeats survive a tick that wraps around.
    {
        MenuPad p;
        MenuPadRaw r = Idle();
        MenuPadUpdate(p, r, 0xFFFFFF00u);
        r.pov = 9000;
        MenuPadUpdate(p, r, 0xFFFFFF10u);
        MenuPadFrame f = MenuPadUpdate(p, r, 0xFFFFFF10u + 351);
        CHECK(f.pressed == kPadRight && f.repeat == kPadRight, "a repeat across the wrap of the tick");
    }
    // The stick: past one half, let go below 0.375, only the larger axis, the direction kept along the edge.
    {
        MenuPad p;
        MenuPadRaw r = Idle();
        MenuPadUpdate(p, r, 0);
        r.y = 32767 - 16000;
        MenuPadFrame f = MenuPadUpdate(p, r, 16);
        CHECK(f.down == 0, "below one half: nothing");
        r.y = 32767 - 17000;
        f = MenuPadUpdate(p, r, 32);
        CHECK(f.pressed == kPadUp, "past one half: up (%x)", f.pressed);
        r.y = 32767 - 13000;
        f = MenuPadUpdate(p, r, 48);
        CHECK(f.down == kPadUp, "still held above the lower mark");
        r.y = 32767 - 12000;
        f = MenuPadUpdate(p, r, 64);
        CHECK(f.down == 0, "let go below it");
        r.y = 32767 - 17000;
        MenuPadUpdate(p, r, 80);
        r.x = 32767 + 16500;  // rolled to the diagonal: up is kept while it stays past the lower mark
        f = MenuPadUpdate(p, r, 96);
        CHECK(f.down == kPadUp && f.pressed == 0, "the direction is kept on the diagonal (%x)", f.down);
        r.y = 32767;
        f = MenuPadUpdate(p, r, 112);
        CHECK(f.down == kPadRight && f.pressed == kPadRight, "then right (%x)", f.down);
        r.x = 32767;
        r.y = 65535;
        f = MenuPadUpdate(p, r, 128);
        CHECK(f.down == kPadDown, "down is a large y (%x)", f.down);
    }
    // The hat's diagonals hold both directions.
    CHECK(MenuPadHat(4500) == (kPadUp | kPadRight), "up-right");
    CHECK(MenuPadHat(22500) == (kPadDown | kPadLeft), "down-left");
    CHECK(MenuPadHat(0) == kPadUp && MenuPadHat(27000) == kPadLeft, "up, left");
    CHECK(MenuPadHat(0xFFFF) == 0 && MenuPadHat(0xFFFFFFFFu) == 0, "centred");
    // In the reader a diagonal keeps the axis already held, and a fresh one is up / down: one frame never moves the row and
    // changes a value at once.
    {
        MenuPad p;
        MenuPadRaw r = Idle();
        MenuPadUpdate(p, r, 0);
        r.pov = 9000;
        MenuPadFrame f = MenuPadUpdate(p, r, 16);
        CHECK(f.pressed == kPadRight, "hat right (%x)", f.pressed);
        r.pov = 4500;
        f = MenuPadUpdate(p, r, 32);
        CHECK(f.down == kPadRight && f.pressed == 0, "rolled onto up-right: still only right (%x %x)", f.down, f.pressed);
        r.pov = 0xFFFF;
        MenuPadUpdate(p, r, 48);
        r.pov = 4500;
        f = MenuPadUpdate(p, r, 64);
        CHECK(f.down == kPadUp && f.pressed == kPadUp, "a fresh up-right is up (%x %x)", f.down, f.pressed);
        r.pov = 0xFFFF;
        MenuPadUpdate(p, r, 80);
        r.pov = 22500;
        f = MenuPadUpdate(p, r, 96);
        CHECK(f.down == kPadDown && f.pressed == kPadDown, "a fresh down-left is down (%x %x)", f.down, f.pressed);
    }
    // Triggers: on the shared axis only when it is the trigger axis; the buttons 10/11 always.
    {
        MenuPad p;
        MenuPadRaw r = Idle();
        MenuPadUpdate(p, r, 0);
        r.z = 32767 + 20000;
        MenuPadFrame f = MenuPadUpdate(p, r, 16);
        CHECK(f.pressed == kPadPageUp, "LT on the axis (%x)", f.pressed);
        r.z = 32767 - 20000;
        f = MenuPadUpdate(p, r, 32);
        CHECK(f.pressed == kPadPageDown, "RT on the axis (%x)", f.pressed);
        r.z = 32767;
        MenuPadUpdate(p, r, 48);
        r.zTriggers = false;
        r.z = 0;
        f = MenuPadUpdate(p, r, 64);
        CHECK(f.down == 0, "the right stick of a DirectInput pad on Z is no trigger");
        r.buttons = kXbRT;
        f = MenuPadUpdate(p, r, 80);
        CHECK(f.pressed == kPadPageDown, "R2 as button 11");
    }
    // The pair that opens and closes the menu, and the rest of the buttons.
    {
        MenuPad p;
        MenuPadRaw r = Idle();
        MenuPadUpdate(p, r, 0);
        r.buttons = kXbBack;
        MenuPadFrame f = MenuPadUpdate(p, r, 16);
        CHECK(f.pressed == 0, "Back alone does nothing");
        r.buttons = kXbBack | kXbStart;
        f = MenuPadUpdate(p, r, 32);
        CHECK(f.pressed == kPadOpen, "the pair (%x)", f.pressed);
        f = MenuPadUpdate(p, r, 2000);
        CHECK(f.pressed == 0, "the pair does not repeat");
        r.buttons = kXbB | kXbY | kXbLB | kXbRB;
        f = MenuPadUpdate(p, r, 2016);
        CHECK(f.pressed == (kPadBack | kPadReset | kPadPrevSection | kPadNextSection), "B Y LB RB (%x)", f.pressed);
    }
    // The menu opened with the pair: it is still held on the first frame of the open menu and must not close it again.
    {
        MenuPad p;
        MenuPadRaw r = Idle();
        r.buttons = kXbBack | kXbStart;
        MenuPadRestart(p);
        MenuPadFrame f = MenuPadUpdate(p, r, 0);
        CHECK(f.pressed == 0, "held at the opening");
        f = MenuPadUpdate(p, r, 500);
        CHECK(f.pressed == 0, "still held");
        r.buttons = 0;
        MenuPadUpdate(p, r, 516);
        r.buttons = kXbBack | kXbStart;
        f = MenuPadUpdate(p, r, 532);
        CHECK(f.pressed == kPadOpen, "pressed again: closes");
    }
    // No pad: nothing is held, and the stick's direction is forgotten.
    {
        MenuPad p;
        MenuPadRaw r = Idle();
        MenuPadUpdate(p, r, 0);
        r.x = 0;
        MenuPadUpdate(p, r, 16);
        MenuPadRaw none = {};
        MenuPadFrame f = MenuPadUpdate(p, none, 32);
        CHECK(f.down == 0 && p.stick == 0, "no pad");
        f = MenuPadUpdate(p, r, 48);
        CHECK(f.pressed == kPadLeft, "the stick again is a new press");
    }
    CHECK(MenuPadStepScale(0) == 1 && MenuPadStepScale(8) == 4 && MenuPadStepScale(24) == 10, "slider steps speed up");
    if (g_fail) {
        printf("menupad: %d checks failed\n", g_fail);
        return 1;
    }
    printf("menupad: all checks passed\n");
    return 0;
}

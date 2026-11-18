#include <stdio.h>
#include <string.h>
#include "dsreport.h"
using namespace ufx;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); ++fails; } } while (0)
int main() {
    // CRC-32 check value of "123456789" is CBF43926 (init FFFFFFFF, final xor)
    const uint8_t s[] = "123456789";
    CHECK(~DsCrc32(s, 9) == 0xCBF43926u, "crc %08X", ~DsCrc32(s, 9));

    DsOutput o;
    o.rightEffect = true;
    o.right = EffectWeapon(4, 6, 5);
    o.leftEffect = true;
    o.left = EffectFeedback(3, 4);
    o.rumble = true; o.motorStrong = 200; o.motorWeak = 90;
    uint8_t usb[64];
    DsBuildUsb(usb, 64, o);
    CHECK(usb[0] == 0x02, "usb id");
    CHECK((usb[1] & 0x0F) == 0x0F, "usb flags0 %02X", usb[1]);
    CHECK(usb[3] == 90 && usb[4] == 200, "usb motors");
    CHECK(usb[11] == 0x25 && usb[12] == ((1 << 4) | (1 << 6)) && usb[13] == 0 && usb[14] == 4, "usb right effect %02X %02X %02X %02X", usb[11], usb[12], usb[13], usb[14]);
    CHECK(usb[22] == 0x21, "usb left effect mode %02X", usb[22]);
    CHECK(usb[23] == 0xF8 && usb[24] == 0x03, "left active zones 3..9: %02X %02X", usb[23], usb[24]);  // 0x3F8
    // strength 4 -> value 3 in each of zones 3..9 (3 bits each)
    uint32_t sz = (uint32_t)usb[25] | ((uint32_t)usb[26] << 8) | ((uint32_t)usb[27] << 16) | ((uint32_t)usb[28] << 24);
    CHECK(((sz >> 9) & 7) == 3 && ((sz >> 27) & 7) == 3 && (sz & 0x1FF) == 0, "strength zones %08X", sz);
    CHECK(usb[39] == 0x04, "flags2");

    uint8_t bt[78];
    DsBuildBt(bt, 5, o);
    CHECK(bt[0] == 0x31 && bt[1] == 0x50 && bt[2] == 0x10, "bt header %02X %02X %02X", bt[0], bt[1], bt[2]);
    CHECK(bt[3] == 0x0F, "bt flags0 %02X", bt[3]);
    CHECK(bt[13] == 0x25 && bt[24] == 0x21, "bt effects %02X %02X", bt[13], bt[24]);
    const uint8_t seed = 0xA2;
    uint32_t crc = ~DsCrc32(bt, 74, DsCrc32(&seed, 1));
    uint32_t got = (uint32_t)bt[74] | ((uint32_t)bt[75] << 8) | ((uint32_t)bt[76] << 16) | ((uint32_t)bt[77] << 24);
    CHECK(crc == got, "bt crc %08X vs %08X", crc, got);

    TriggerEffect v = EffectVibration(2, 6, 25);
    CHECK(v.data[0] == 0x26 && v.data[9] == 25 && v.data[1] == 0xFC, "vibration %02X %02X %02X", v.data[0], v.data[9], v.data[1]);
    CHECK(EffectOff().data[0] == 0x05, "off");

    // Input: a Bluetooth simple report 01h arrives padded to 78 bytes on Windows; it must not be read with the USB layout.
    uint8_t simple[78] = {};
    simple[0] = 0x01; simple[1] = 0x80; simple[2] = 0x80; simple[3] = 0x80; simple[4] = 0x80;
    simple[5] = 0x08;  // hat none, no face button
    simple[6] = 0x02;  // R1
    simple[8] = 0x00; simple[9] = 0xFF;  // L2 released, R2 fully down
    DsInput in{};
    CHECK(DsParseInput(simple, 78, true, &in), "bt simple parsed");
    CHECK(in.hat == 8 && in.face == 0 && in.shoulder == 0x02 && in.l2 == 0 && in.r2 == 0xFF && in.lx == 0x80,
          "bt simple: hat %u face %u shoulder %02X l2 %u r2 %u", in.hat, in.face, in.shoulder, in.l2, in.r2);
    // USB report 01h: L2 5, R2 6, hat/face 8, shoulders 9, PS/touch 10.
    uint8_t usbIn[64] = {};
    usbIn[0] = 0x01; usbIn[1] = 0x10; usbIn[5] = 0x40; usbIn[6] = 0x90; usbIn[8] = 0x22; usbIn[9] = 0x01; usbIn[10] = 0x02;
    CHECK(DsParseInput(usbIn, 64, false, &in), "usb parsed");
    CHECK(in.lx == 0x10 && in.l2 == 0x40 && in.r2 == 0x90 && in.hat == 2 && in.face == 2 && in.shoulder == 0x01 && in.special == 2,
          "usb: lx %02X l2 %02X r2 %02X hat %u face %u shoulder %02X special %u", in.lx, in.l2, in.r2, in.hat, in.face, in.shoulder, in.special);
    // Bluetooth full report 31h: the same fields one byte later.
    uint8_t btIn[78] = {};
    btIn[0] = 0x31; btIn[2] = 0x10; btIn[6] = 0x40; btIn[7] = 0x90; btIn[9] = 0x22; btIn[10] = 0x01; btIn[11] = 0x02;
    CHECK(DsParseInput(btIn, 78, true, &in), "bt full parsed");
    CHECK(in.lx == 0x10 && in.l2 == 0x40 && in.r2 == 0x90 && in.hat == 2 && in.face == 2 && in.shoulder == 0x01 && in.special == 2,
          "bt full: lx %02X l2 %02X r2 %02X hat %u", in.lx, in.l2, in.r2, in.hat);
    CHECK(!DsParseInput(usbIn, 8, false, &in), "short report rejected");
    printf("%s\n", fails ? "DSREPORT TESTS FAILED" : "DSREPORT TESTS PASSED");
    return fails ? 1 : 0;
}

// DualSense reports: input parsing, rumble and adaptive trigger effects. Pure functions without Windows headers, tested on the host
// (tests/test_dsreport.cpp). Layouts follow the public documentation of the DualSense (Linux hid-playstation, the
// community reverse-engineering notes); they are checked on a real pad through the menu test in F10.
//
// Common output data (47 bytes):
//   0 flags0 (bit0 compatible rumble, bit1 haptics select, bit2 right trigger effect, bit3 left trigger effect)
//   1 flags1   2 right motor (weak)   3 left motor (strong)   ...
//   10..20 right trigger effect (11 bytes: mode, 10 parameters)   21..31 left trigger effect
//   38 flags2 (bit2 compatible rumble v2)
// USB: report id 02h, the common data follows. Bluetooth: report id 31h, a sequence/tag byte, tag 10h, the common data,
// padding up to 74 bytes, then CRC-32 of (A2h + the first 74 bytes), little endian.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace ufx {

struct TriggerEffect {
    uint8_t data[11];  // mode + 10 parameters
};

struct DsOutput {
    bool rightEffect = false, leftEffect = false;  // send the trigger effects
    TriggerEffect right{}, left{};
    bool rumble = false;                           // send the motors
    uint8_t motorStrong = 0, motorWeak = 0;
};

inline uint32_t DsCrc32(const uint8_t* data, size_t len, uint32_t crc = 0xFFFFFFFFu) {
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}

inline void DsFillCommon(uint8_t* c, const DsOutput& o) {
    if (o.rumble) {
        c[0] |= 0x01 | 0x02;
        c[2] = o.motorWeak;
        c[3] = o.motorStrong;
        c[38] |= 0x04;
    }
    if (o.rightEffect) {
        c[0] |= 0x04;
        for (int i = 0; i < 11; ++i) c[10 + i] = o.right.data[i];
    }
    if (o.leftEffect) {
        c[0] |= 0x08;
        for (int i = 0; i < 11; ++i) c[21 + i] = o.left.data[i];
    }
}

// USB: `out` must hold `length` bytes (the output report length of the device, 64 or 63); it is zeroed here.
inline size_t DsBuildUsb(uint8_t* out, size_t length, const DsOutput& o) {
    for (size_t i = 0; i < length; ++i) out[i] = 0;
    out[0] = 0x02;
    DsFillCommon(out + 1, o);
    return length;
}

// Bluetooth: report 31h, 78 bytes. `seq` is a counter that is increased on every report (0..15).
inline size_t DsBuildBt(uint8_t* out, uint8_t seq, const DsOutput& o) {
    const size_t length = 78;
    for (size_t i = 0; i < length; ++i) out[i] = 0;
    out[0] = 0x31;
    out[1] = (uint8_t)((seq & 0x0F) << 4);
    out[2] = 0x10;
    DsFillCommon(out + 3, o);
    const uint8_t seed = 0xA2;
    uint32_t crc = DsCrc32(&seed, 1);
    crc = ~DsCrc32(out, length - 4, crc);
    out[74] = (uint8_t)crc;
    out[75] = (uint8_t)(crc >> 8);
    out[76] = (uint8_t)(crc >> 16);
    out[77] = (uint8_t)(crc >> 24);
    return length;
}

// ---- input reports ----------------------------------------------------------------------------------------------
//   USB report 01h (64 bytes):  1 LX, 2 LY, 3 RX, 4 RY, 5 L2, 6 R2, 7 counter, 8 hat + face, 9 shoulders, 10 PS/touch
//   Bluetooth report 31h:       same fields, shifted by one byte (the first byte is a tag)
//   Bluetooth simple report 01h (before the pad is switched to the full mode): 1..4 sticks, 5 hat + face,
//                               6 shoulders, 7 PS/touch, 8 L2, 9 R2
// The layout of a 01h report is chosen by the connection, not by the length that was read: on Windows ReadFile returns
// InputReportByteLength bytes for every report (78 on Bluetooth), padded with zeros, so a Bluetooth simple report arrives
// as 78 bytes as well.
struct DsInput {
    uint8_t lx, ly, rx, ry, l2, r2;
    uint8_t hat;      // 0..7, 8 = none
    uint8_t face;     // bit 0 square, 1 cross, 2 circle, 3 triangle
    uint8_t shoulder; // L1 R1 L2 R2 Create Options L3 R3 (bits 0..7)
    uint8_t special;  // bit 0 PS, bit 1 touchpad
};

// Turns one input report into a DsInput; false if the report is not an input report we understand.
inline bool DsParseInput(const uint8_t* b, int length, bool bluetooth, DsInput* s) {
    int o;      // offset of LX
    int hat;    // offset of the hat/face byte
    bool simple = false;
    if (b[0] == 0x31 && length >= 12) {  // Bluetooth, full mode
        o = 2;
        hat = 9;
    } else if (b[0] == 0x01 && bluetooth && length >= 10) {  // Bluetooth simple report (padded to 78 bytes on Windows)
        o = 1;
        hat = 5;
        simple = true;
    } else if (b[0] == 0x01 && !bluetooth && length >= 60) {  // USB
        o = 1;
        hat = 8;
    } else {
        return false;
    }
    s->lx = b[o]; s->ly = b[o + 1]; s->rx = b[o + 2]; s->ry = b[o + 3];
    if (simple) {
        s->l2 = b[8]; s->r2 = b[9];
        s->shoulder = b[6];
        s->special = b[7] & 3;
    } else {
        s->l2 = b[o + 4]; s->r2 = b[o + 5];
        s->shoulder = b[hat + 1];
        s->special = b[hat + 2] & 3;
    }
    s->hat = b[hat] & 0x0F;
    if (s->hat > 8) s->hat = 8;
    s->face = (uint8_t)(b[hat] >> 4);
    return true;
}

// ---- trigger effects (zones 0..9 along the travel of the trigger; strength 1..8) ---------------------------------
inline TriggerEffect EffectOff() {
    TriggerEffect e{};
    e.data[0] = 0x05;
    return e;
}

inline uint32_t ZoneMask(int from, int to) {  // zones from..to inclusive
    uint32_t m = 0;
    for (int z = from; z <= to && z < 10; ++z) m |= 1u << z;
    return m;
}

inline void PutZones(TriggerEffect& e, uint32_t active, uint32_t strengthZones) {
    e.data[1] = (uint8_t)active;
    e.data[2] = (uint8_t)(active >> 8);
    e.data[3] = (uint8_t)strengthZones;
    e.data[4] = (uint8_t)(strengthZones >> 8);
    e.data[5] = (uint8_t)(strengthZones >> 16);
    e.data[6] = (uint8_t)(strengthZones >> 24);
}

inline uint32_t Packed(int from, int to, int strength) {
    uint32_t s = 0;
    for (int z = from; z <= to && z < 10; ++z) s |= (uint32_t)((strength - 1) & 7) << (3 * z);
    return s;
}

// Resistance from `position` to the end of the travel.
inline TriggerEffect EffectFeedback(int position, int strength) {
    TriggerEffect e{};
    if (strength <= 0) return EffectOff();
    if (strength > 8) strength = 8;
    e.data[0] = 0x21;
    PutZones(e, ZoneMask(position, 9), Packed(position, 9, strength));
    return e;
}

// A click like a gun trigger: resistance between two zones, then it gives way.
inline TriggerEffect EffectWeapon(int start, int end, int strength) {
    TriggerEffect e{};
    if (strength <= 0) return EffectOff();
    if (strength > 8) strength = 8;
    if (start < 2) start = 2;
    if (start > 7) start = 7;
    if (end <= start) end = start + 1;
    if (end > 8) end = 8;
    uint32_t zones = (1u << start) | (1u << end);
    e.data[0] = 0x25;
    e.data[1] = (uint8_t)zones;
    e.data[2] = (uint8_t)(zones >> 8);
    e.data[3] = (uint8_t)((strength - 1) & 7);
    return e;
}

// Vibration from `position` on while the trigger is held; `frequency` in hertz.
inline TriggerEffect EffectVibration(int position, int amplitude, int frequency) {
    TriggerEffect e{};
    if (amplitude <= 0 || frequency <= 0) return EffectOff();
    if (amplitude > 8) amplitude = 8;
    e.data[0] = 0x26;
    PutZones(e, ZoneMask(position, 9), Packed(position, 9, amplitude));
    e.data[9] = (uint8_t)frequency;
    return e;
}

}  // namespace ufx

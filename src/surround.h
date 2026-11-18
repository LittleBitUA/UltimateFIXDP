// Speaker layout and matrix arithmetic of the surround fix (proxy.cpp). No Windows headers here: tests/test_surround.cpp
// runs it on the host.
//
// A matrix is laid out as XAudio2, XACT3 and X3DAudio want it: the level from source channel S to destination channel D
// is at [sources * D + S]. The destination channels of a mix follow the bits of its speaker mask from the lowest up.
#pragma once
#include <stdint.h>

namespace ufx {

constexpr uint32_t kSpkFrontLeft = 0x1, kSpkFrontRight = 0x2, kSpkCentre = 0x4, kSpkSub = 0x8, kSpkBackLeft = 0x10,
                   kSpkBackRight = 0x20, kSpkSideLeft = 0x200, kSpkSideRight = 0x400;
constexpr uint32_t kSurroundMaxSpeakers = 8;  // 2 sources x 8 speakers is what the game's matrix buffer holds

struct SpeakerLayout {
    uint32_t mask;
    uint32_t speakers;     // bits set in the mask
    bool known;            // 3..8 speakers with a front pair, one speaker per channel of the mix
    float panLeft[kSurroundMaxSpeakers];   // 2D pan: the share of the left source channel each destination channel gets
    float panRight[kSurroundMaxSpeakers];  // the same for the right one
    bool six;              // the mix has every speaker a 5.1 wave needs
    uint32_t sixTarget[6]; // destination channel of each channel of a 5.1 wave (FL FR FC LFE and the rear pair)
};

inline uint32_t CountSpeakers(uint32_t mask) {
    uint32_t n = 0;
    for (; mask; mask &= mask - 1) ++n;
    return n;
}

// Destination channel of one speaker of the mask.
inline uint32_t ChannelOfSpeaker(uint32_t mask, uint32_t speaker) { return CountSpeakers(mask & (speaker - 1)); }

inline SpeakerLayout ReadSpeakerLayout(uint32_t mask, uint32_t mixChannels) {
    SpeakerLayout l = {};
    l.mask = mask;
    l.speakers = CountSpeakers(mask);
    l.known = l.speakers >= 3 && l.speakers <= kSurroundMaxSpeakers && l.speakers == mixChannels &&
              (mask & kSpkFrontLeft) && (mask & kSpkFrontRight);
    if (!l.known) return l;
    // The Xbox 360 sends a 2D sound to the front pair at 1.0 and to its one rear pair at 0.8. A mix with two rear speakers
    // on a side (7.1) gives each of them 0.8 / sqrt(2), so that the power behind the listener stays what 5.1 has.
    constexpr float kRear = 0.8f, kRearOfTwo = 0.8f * 0.70710678f;
    float rearLeft = (mask & kSpkBackLeft) && (mask & kSpkSideLeft) ? kRearOfTwo : kRear;
    float rearRight = (mask & kSpkBackRight) && (mask & kSpkSideRight) ? kRearOfTwo : kRear;
    l.panLeft[ChannelOfSpeaker(mask, kSpkFrontLeft)] = 1.0f;
    l.panRight[ChannelOfSpeaker(mask, kSpkFrontRight)] = 1.0f;
    if (mask & kSpkBackLeft) l.panLeft[ChannelOfSpeaker(mask, kSpkBackLeft)] = rearLeft;
    if (mask & kSpkSideLeft) l.panLeft[ChannelOfSpeaker(mask, kSpkSideLeft)] = rearLeft;
    if (mask & kSpkBackRight) l.panRight[ChannelOfSpeaker(mask, kSpkBackRight)] = rearRight;
    if (mask & kSpkSideRight) l.panRight[ChannelOfSpeaker(mask, kSpkSideRight)] = rearRight;
    // A 5.1 wave: its rear pair goes to the back speakers when the mix has them, otherwise to the side ones.
    bool back = (mask & kSpkBackLeft) && (mask & kSpkBackRight);
    const uint32_t wave[6] = {kSpkFrontLeft, kSpkFrontRight, kSpkCentre, kSpkSub, back ? kSpkBackLeft : kSpkSideLeft,
                              back ? kSpkBackRight : kSpkSideRight};
    l.six = true;
    for (int s = 0; s < 6; ++s) {
        if (!(mask & wave[s])) l.six = false;
        l.sixTarget[s] = ChannelOfSpeaker(mask, wave[s]);
    }
    return l;
}

// The 2D stereo pan of the Xbox 360 code (Function_82555F10, wave kinds 0 and 1): left = |1 - pan| / 2, right = |-1 - pan| / 2,
// each times the volume, to its front speaker and its rear ones; centre and subwoofer stay silent. `m` gets 2 x speakers
// floats. The pan is held inside -1..1, the range the formula is written for (outside it one side would be boosted).
inline void BuildPanMatrix(const SpeakerLayout& l, float pan, float volume, float* m) {
    if (pan < -1.0f) pan = -1.0f;
    if (pan > 1.0f) pan = 1.0f;
    float left = (1.0f - pan) * 0.5f * volume, right = (1.0f + pan) * 0.5f * volume;
    for (uint32_t d = 0; d < l.speakers; ++d) {
        m[d * 2] = left * l.panLeft[d];
        m[d * 2 + 1] = right * l.panRight[d];
    }
}

// A six-channel wave (kinds 2 and 3 of the Xbox code: a 6 x 6 diagonal): each channel to its own speaker at the volume.
// `m` gets 6 x speakers floats. Only for a layout with `six`.
inline void BuildSixMatrix(const SpeakerLayout& l, float volume, float* m) {
    for (uint32_t i = 0; i < 6 * l.speakers; ++i) m[i] = 0.0f;
    for (uint32_t s = 0; s < 6; ++s) m[l.sixTarget[s] * 6 + s] = volume;
}

// A 3D voice: the 2 x speakers matrix X3DAudio calculated, every coefficient times the volume of the voice (`full`), and
// the same folded to one source, the mean of the two columns (`mono`, speakers floats).
inline void ScaleVoiceMatrix(const float* x3d, uint32_t speakers, float volume, float* full, float* mono) {
    for (uint32_t d = 0; d < speakers; ++d) {
        full[d * 2] = x3d[d * 2] * volume;
        full[d * 2 + 1] = x3d[d * 2 + 1] * volume;
        mono[d] = (full[d * 2] + full[d * 2 + 1]) * 0.5f;
    }
}

}  // namespace ufx

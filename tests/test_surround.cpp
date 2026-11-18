// Host test of the speaker layout and matrix arithmetic of the surround fix (src/surround.h).
// Build:  make test
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "surround.h"

using namespace ufx;

static int g_failures = 0;

static void Check(bool ok, const char* what) {
    if (!ok) {
        printf("FAIL  %s\n", what);
        ++g_failures;
    }
}

static bool Near(float a, float b) { return fabsf(a - b) < 1e-6f; }

// Every coefficient of `m` is zero except the listed (index, value) pairs.
static bool Only(const float* m, int count, const int* index, const float* value, int listed) {
    for (int i = 0; i < count; ++i) {
        float want = 0.0f;
        for (int k = 0; k < listed; ++k)
            if (index[k] == i) want = value[k];
        if (!Near(m[i], want)) return false;
    }
    return true;
}

int main() {
    // Layouts that are left alone: stereo, mono, a mask that does not match the mix, no front pair, more than 8 speakers.
    Check(!ReadSpeakerLayout(0x3, 2).known, "stereo is not a surround layout");
    Check(!ReadSpeakerLayout(0x4, 1).known, "mono is not a surround layout");
    Check(!ReadSpeakerLayout(0x3F, 2).known, "5.1 mask with a stereo mix is refused");
    Check(!ReadSpeakerLayout(0, 6).known, "an empty mask with a 6-channel mix is refused");
    Check(!ReadSpeakerLayout(0x3C, 4).known, "no front pair is refused");
    Check(!ReadSpeakerLayout(0x7FF, 11).known, "11 speakers are refused");

    // 5.1 with back speakers (3Fh: FL FR FC LFE BL BR), what the Xbox 360 mixes to.
    SpeakerLayout l51 = ReadSpeakerLayout(0x3F, 6);
    Check(l51.known && l51.speakers == 6 && l51.six, "5.1: known, 6 speakers, room for a 5.1 wave");
    float m[48];
    memset(m, 0x7F, sizeof(m));
    BuildPanMatrix(l51, 0.0f, 1.0f, m);
    {   // The Xbox matrix, row per source: L -> {1, 0, 0, 0, 0.8, 0}, R -> {0, 1, 0, 0, 0, 0.8}, each times 0.5 at the centre.
        // Transposed to [2 * destination + source]: FL<-L at 0, FR<-R at 3, BL<-L at 8, BR<-R at 11.
        const int index[4] = {0, 3, 8, 11};
        const float value[4] = {0.5f, 0.5f, 0.4f, 0.4f};
        Check(Only(m, 12, index, value, 4), "5.1 pan, centre: fronts 0.5, rears 0.4, centre and subwoofer silent");
    }
    BuildPanMatrix(l51, -1.0f, 2.0f, m);
    {
        const int index[2] = {0, 8};
        const float value[2] = {2.0f, 1.6f};
        Check(Only(m, 12, index, value, 2), "5.1 pan, full left at volume 2: left front 2.0, left rear 1.6, right side silent");
    }
    BuildPanMatrix(l51, 5.0f, 1.0f, m);
    {
        const int index[2] = {3, 11};
        const float value[2] = {1.0f, 0.8f};
        Check(Only(m, 12, index, value, 2), "5.1 pan beyond the range is held at full right");
    }
    BuildSixMatrix(l51, 0.25f, m);
    {
        const int index[6] = {0, 7, 14, 21, 28, 35};
        const float value[6] = {0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f};
        Check(Only(m, 36, index, value, 6), "5.1 wave on 5.1: the 6 x 6 diagonal of the Xbox code");
    }

    // 5.1 with side speakers (60Fh: FL FR FC LFE SL SR): the rear pair is channels 4 and 5 as well.
    SpeakerLayout side = ReadSpeakerLayout(0x60F, 6);
    Check(side.known && side.six, "5.1 side: known");
    BuildPanMatrix(side, 0.0f, 1.0f, m);
    {
        const int index[4] = {0, 3, 8, 11};
        const float value[4] = {0.5f, 0.5f, 0.4f, 0.4f};
        Check(Only(m, 12, index, value, 4), "5.1 side pan: the same matrix as 5.1 back");
    }
    BuildSixMatrix(side, 1.0f, m);
    {
        const int index[6] = {0, 7, 14, 21, 28, 35};
        const float value[6] = {1, 1, 1, 1, 1, 1};
        Check(Only(m, 36, index, value, 6), "5.1 wave on 5.1 side: diagonal");
    }

    // 7.1 (63Fh: FL FR FC LFE BL BR SL SR): two rear speakers a side share the rear power.
    SpeakerLayout l71 = ReadSpeakerLayout(0x63F, 8);
    Check(l71.known && l71.speakers == 8 && l71.six, "7.1: known");
    BuildPanMatrix(l71, 0.0f, 1.0f, m);
    {
        const float r = 0.5f * 0.8f * 0.70710678f;
        const int index[6] = {0, 3, 8, 11, 12, 15};
        const float value[6] = {0.5f, 0.5f, r, r, r, r};
        Check(Only(m, 16, index, value, 6), "7.1 pan: fronts 0.5, back and side pairs 0.8 / sqrt(2) of that");
        Check(Near(2 * r * r, 0.4f * 0.4f), "7.1 pan: the rear power of a side equals the 5.1 one");
    }
    BuildSixMatrix(l71, 1.0f, m);
    {   // [6 * destination + source]: FL, FR, FC, LFE on themselves, the rear pair of the wave on BL (4) and BR (5).
        const int index[6] = {0, 7, 14, 21, 28, 35};
        const float value[6] = {1, 1, 1, 1, 1, 1};
        Check(Only(m, 48, index, value, 6), "5.1 wave on 7.1: back speakers get the rear pair, side speakers silent");
    }

    // Quad (33h: FL FR BL BR) and 2.1 (0Bh: FL FR LFE): a pan matrix, but no place for a 5.1 wave.
    SpeakerLayout quad = ReadSpeakerLayout(0x33, 4);
    Check(quad.known && !quad.six, "quad: known, no room for a 5.1 wave");
    BuildPanMatrix(quad, 0.0f, 1.0f, m);
    {
        const int index[4] = {0, 3, 4, 7};
        const float value[4] = {0.5f, 0.5f, 0.4f, 0.4f};
        Check(Only(m, 8, index, value, 4), "quad pan: fronts on channels 0 and 1, rears on 2 and 3");
    }
    SpeakerLayout two1 = ReadSpeakerLayout(0xB, 3);
    Check(two1.known && !two1.six, "2.1: known, no room for a 5.1 wave");
    BuildPanMatrix(two1, 0.0f, 1.0f, m);
    {
        const int index[2] = {0, 3};
        const float value[2] = {0.5f, 0.5f};
        Check(Only(m, 6, index, value, 2), "2.1 pan: fronts only, subwoofer silent");
    }

    // A 3D voice: every coefficient times the volume, and the fold to one source.
    const float x3d[12] = {0.5f, 0.25f, 0.5f, 0.75f, 1.0f, 0.0f, 0.0f, 0.0f, 0.125f, 0.5f, 0.0f, 1.0f};
    float full[16], mono[8];
    ScaleVoiceMatrix(x3d, 6, 0.5f, full, mono);
    bool scaled = true;
    for (int i = 0; i < 12; ++i) scaled &= Near(full[i], x3d[i] * 0.5f);
    for (int d = 0; d < 6; ++d) scaled &= Near(mono[d], (x3d[d * 2] + x3d[d * 2 + 1]) * 0.25f);
    Check(scaled, "3D voice: matrix times the volume, one-source fold is the mean of the two columns");

    printf("%s\n", g_failures ? "SURROUND TESTS FAILED" : "SURROUND TESTS PASSED");
    return g_failures ? 1 : 0;
}

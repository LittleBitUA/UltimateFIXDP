// FarShadowFilter ([Graphics] FarShadowFilter=1): the far sun shadow cascade filtered like the near two, as a token patch of
// the game's own shader (used by src/d3d/trace.cpp).
// The sun shadow receivers (24 ps_3_0 shaders whose constant table names g_vCascade and g_tDepth2: 12 plain from A17B74h,
// 12 with a spot light from A1E48Ch) read the two near cascades (s4, s5) with four projected taps half a texel apart
// (+-0.5 / g_fTexsize, c0.x) and average them, but the far cascade (s6) with ONE tap: `texldp rD, rC, s6`, of which only
// rD.x is read (the far branch: `mov rN.w, rD.x`). Far shadow edges are therefore hard and crawl. A copy of each shader gets
// that single fetch replaced by the near cascades' own four taps, token by token (exact pattern, no D3DX text round trip):
//   def c31, 0.5, -0.5, 0.25, 0          (inserted before the first def; c31 is never used by these shaders)
//   rcp rA.x, c0.x / mul rA.xy, rA.x, c31.xy           half a texel: (+h, -h)
//   mov rB.zw, rC                                      depth and w (1) as the game passes them
//   4 x { add rB.xy, rC, rA.<xx|yy|xy|yx> / texldp rT, rB, s6 / sum in rA.z }
//   mul rD, rA.z, c31.z                                the average in all four components (only .x is read)
// rA, rB, rT are the three temps above the highest the shader uses. Checked offline on all 24 shaders of DP.exe 1.01b
// (every one matches; the fetch is outside flow control and rD.yzw are written before they are read). A shader that differs
// in any point is kept as the game's. No Windows headers: tested on the host against DP.exe (tests/test_farshadow.cpp).
#pragma once
#include <stdint.h>

namespace ufx {

constexpr int kFarShadowMaxWords = 2048;
constexpr uint32_t kFarTexldp = 0x03010042u;  // texld with the projective control bit, as the game's shadow taps
constexpr uint32_t kFarS4 = 0xA0E40804u, kFarS5 = 0xA0E40805u, kFarS6 = 0xA0E40806u;  // sampler source tokens s4, s5, s6

inline uint32_t FarRegType(uint32_t t) { return ((t >> 28) & 7) | ((t >> 8) & 0x18); }

// Builds the patched copy into out (kFarShadowMaxWords). Returns its length in DWORDs, or 0 with *why set when the pattern is
// not exact (the shader is then kept as the game's).
inline int PatchFarCascade(const uint32_t* code, uint32_t* out, const char** why) {
    *why = "not ps_3_0";
    if (!code || code[0] != 0xFFFF0300u) return 0;
    int firstDef = -1, firstIns = -1, fetch = -1, end = -1, s4 = 0, s5 = 0, s6 = 0, depth = 0, fetchDepth = -1, maxTemp = -1;
    bool c31 = false;
    for (int i = 1; i < 1900;) {
        const uint32_t tok = code[i];
        if (tok == 0x0000FFFFu) { end = i; break; }
        if ((tok & 0xFFFF) == 0xFFFE) { i += 1 + (int)((tok >> 16) & 0x7FFF); continue; }  // comment (the constant table)
        const uint32_t op = tok & 0xFFFF;
        const int len = (int)((tok >> 24) & 0xF);
        const uint32_t* p = code + i + 1;
        if (firstIns < 0) firstIns = i;
        if (op == 0x51 || op == 0x30 || op == 0x2F) {  // def, defi, defb: only the register, the values are not tokens
            if (firstDef < 0) firstDef = i;
            if (len && FarRegType(p[0]) == 2 && (p[0] & 0x7FF) == 31) c31 = true;
        } else if (op == 0x1F) {  // dcl: the first token is the usage, not a register
            if (len >= 2 && FarRegType(p[1]) == 0 && (int)(p[1] & 0x7FF) > maxTemp) maxTemp = (int)(p[1] & 0x7FF);
        } else {
            for (int k = 0; k < len; ++k) {
                const uint32_t t = p[k];
                if (!(t & 0x80000000u)) continue;
                if (FarRegType(t) == 0 && (int)(t & 0x7FF) > maxTemp) maxTemp = (int)(t & 0x7FF);
                if (FarRegType(t) == 2 && (t & 0x7FF) == 31) c31 = true;
                if (FarRegType(t) == 10 && (t & 0x7FF) == 6) ++s6;  // every use of s6, whatever the instruction
            }
            if (tok == kFarTexldp && len == 3) {
                if (p[2] == kFarS4) ++s4;
                if (p[2] == kFarS5) ++s5;
                if (p[2] == kFarS6) { fetch = i; fetchDepth = depth; }
            }
            if (op == 0x28 || op == 0x29 || op == 0x26 || op == 0x1B) ++depth;  // if, ifc, rep, loop
            if (op == 0x2B || op == 0x27 || op == 0x1D) --depth;                // endif, endrep, endloop
        }
        i += 1 + len;
    }
    *why = "no end token";
    if (end < 0) return 0;
    *why = "the far cascade is not one projected tap";
    if (s6 != 1 || fetch < 0) return 0;
    *why = "the near cascades are not four taps each";
    if (s4 != 4 || s5 != 4) return 0;
    const uint32_t dTok = code[fetch + 1], cTok = code[fetch + 2];
    *why = "unexpected registers at the far tap";
    if ((dTok & 0xFFFFF800u) != 0x800F0000u || (cTok & 0xFFFFF800u) != 0x80E40000u || fetchDepth != 0) return 0;
    *why = "c31 is in use";
    if (c31) return 0;
    *why = "no free temp registers";
    if (maxTemp < 0 || maxTemp + 3 >= 32) return 0;
    *why = "too long";
    if (end + 1 + 6 + 61 - 4 > kFarShadowMaxWords) return 0;
    const uint32_t D = dTok & 0x7FF, C = cTok & 0x7FF, A = (uint32_t)maxTemp + 1, B = (uint32_t)maxTemp + 2, T = (uint32_t)maxTemp + 3;
    auto dst = [](uint32_t r, uint32_t mask) { return 0x80000000u | (mask << 16) | r; };
    auto src = [](uint32_t r, uint32_t swz) { return 0x80000000u | (swz << 16) | r; };
    int n = 0;
    const int at = firstDef >= 0 ? firstDef : firstIns;
    for (int i = 0; i < at; ++i) out[n++] = code[i];
    const uint32_t def[6] = {0x05000051u, 0xA00F001Fu, 0x3F000000u, 0xBF000000u, 0x3E800000u, 0};  // def c31, 0.5, -0.5, 0.25, 0
    for (int k = 0; k < 6; ++k) out[n++] = def[k];
    for (int i = at; i < fetch; ++i) out[n++] = code[i];
    const uint32_t head[10] = {0x02000006u, dst(A, 1), 0xA0000000u,                   // rcp rA.x, c0.x
                               0x03000005u, dst(A, 3), src(A, 0x00), 0xA0E4001Fu,  // mul rA.xy, rA.x, c31.xy
                               0x02000001u, dst(B, 0xC), src(C, 0xE4)};            // mov rB.zw, rC
    for (int k = 0; k < 10; ++k) out[n++] = head[k];
    static const uint32_t kSwz[4] = {0x00, 0x55, 0xE4, 0xE1};  // rA.xx (+h,+h), .yy (-h,-h), .xy (+h,-h), .yx (-h,+h)
    for (int t = 0; t < 4; ++t) {
        const uint32_t tap[8] = {0x03000002u, dst(B, 3), src(C, 0xE4), src(A, kSwz[t]),  // add rB.xy, rC, offset
                                 kFarTexldp, dst(T, 0xF), src(B, 0xE4), kFarS6};        // texldp rT, rB, s6
        for (int k = 0; k < 8; ++k) out[n++] = tap[k];
        if (t == 0) {  // mov rA.z, rT.x
            out[n++] = 0x02000001u; out[n++] = dst(A, 4); out[n++] = src(T, 0x00);
        } else {       // add rA.z, rA.z, rT.x
            out[n++] = 0x03000002u; out[n++] = dst(A, 4); out[n++] = src(A, 0xAA); out[n++] = src(T, 0x00);
        }
    }
    out[n++] = 0x03000005u; out[n++] = dst(D, 0xF); out[n++] = src(A, 0xAA); out[n++] = 0xA0AA001Fu;  // mul rD, rA.z, c31.z
    for (int i = fetch + 4; i <= end; ++i) out[n++] = code[i];
    *why = "";
    return n;
}

}  // namespace ufx

// FarShadowFilter (src/farshadow.h) on the real shaders of DP.exe 1.01b: every sun shadow receiver (ps_3_0 whose constant table
// names g_vCascade and g_tDepth2) must be patched, and the result must be exactly the bytecode an independent script built and
// checked by disassembly (same length, same FNV-1a). Without a DP.exe only the checks on a damaged copy run.
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "farshadow.h"

static uint32_t Fnv(const uint32_t* w, int n) {
    uint32_t h = 0x811C9DC5u;
    for (int i = 0; i < n; ++i)
        for (int b = 0; b < 4; ++b) h = (h ^ ((w[i] >> (8 * b)) & 0xFF)) * 0x01000193u;
    return h;
}

struct Expect { uint32_t va, words, fnv; };
static const Expect kExpect[24] = {
    {0x00A17B74u, 385u, 0x3408BC73u}, {0x00A1807Cu, 386u, 0xAD500329u}, {0x00A18588u, 385u, 0x3408BC73u},
    {0x00A18A90u, 386u, 0xAD500329u}, {0x00A18F9Cu, 385u, 0x3408BC73u}, {0x00A194A4u, 386u, 0xAD500329u},
    {0x00A1A8A4u, 436u, 0x8C3720D2u}, {0x00A1AE78u, 437u, 0xA711A5FCu}, {0x00A1B450u, 436u, 0x8C3720D2u},
    {0x00A1BA24u, 437u, 0xA711A5FCu}, {0x00A1BFFCu, 436u, 0x8C3720D2u}, {0x00A1C5D0u, 437u, 0xA711A5FCu},
    {0x00A1E48Cu, 491u, 0x11F42174u}, {0x00A1EB3Cu, 491u, 0xF7FD51F6u}, {0x00A1F1ECu, 491u, 0x11F42174u},
    {0x00A1F89Cu, 491u, 0xF7FD51F6u}, {0x00A1FF4Cu, 491u, 0x11F42174u}, {0x00A205FCu, 491u, 0xF7FD51F6u},
    {0x00A22590u, 542u, 0x9246C835u}, {0x00A22D0Cu, 542u, 0xDCD0D873u}, {0x00A23488u, 542u, 0x9246C835u},
    {0x00A23C04u, 542u, 0xDCD0D873u}, {0x00A24380u, 542u, 0x9246C835u}, {0x00A24AFCu, 542u, 0xDCD0D873u},
};

static std::vector<uint8_t> g_file;
struct Sec { uint32_t va, size, raw, rawSize; };
static std::vector<Sec> g_secs;

static uint32_t U32(size_t off) { uint32_t v; memcpy(&v, &g_file[off], 4); return v; }
static long OffsetOf(uint32_t va) {
    for (const Sec& s : g_secs)
        if (va >= 0x400000u + s.va && va < 0x400000u + s.va + s.rawSize) return (long)(va - 0x400000u - s.va + s.raw);
    return -1;
}
static uint32_t VaOf(size_t off) {
    for (const Sec& s : g_secs)
        if (off >= s.raw && off < s.raw + s.rawSize) return (uint32_t)(0x400000u + s.va + off - s.raw);
    return 0;
}
static bool CtabNames(size_t off, const char* name) {
    const uint32_t tok = U32(off + 4);
    if ((tok & 0xFFFF) != 0xFFFE) return false;
    const size_t len = ((tok >> 16) & 0x7FFF) * 4, n = strlen(name);
    for (size_t i = 0; i + n <= len && off + 8 + i + n <= g_file.size(); ++i)
        if (!memcmp(&g_file[off + 8 + i], name, n)) return true;
    return false;
}
static std::vector<uint32_t> Words(long off) {
    std::vector<uint32_t> w(4096, 0x0000FFFFu);
    for (size_t i = 0; i < w.size() && off + 4 * i + 4 <= g_file.size(); ++i) w[i] = U32(off + 4 * i);
    return w;
}

int main(int argc, char** argv) {
    int bad = 0;
    static uint32_t out[ufx::kFarShadowMaxWords];
    const char* why = "";
    // A vertex shader and an empty pixel shader are refused.
    const uint32_t vs[2] = {0xFFFE0300u, 0x0000FFFFu}, ps[2] = {0xFFFF0300u, 0x0000FFFFu};
    if (ufx::PatchFarCascade(vs, out, &why) || ufx::PatchFarCascade(ps, out, &why)) { printf("FAIL: an unrelated shader was patched\n"); ++bad; }
    FILE* f = argc > 1 && argv[1][0] ? fopen(argv[1], "rb") : nullptr;
    if (!f) {
        printf("%s\n", bad ? "FARSHADOW TEST FAILED" : "farshadow: no DP.exe given, only the synthetic checks ran");
        return bad ? 1 : 0;
    }
    fseek(f, 0, SEEK_END);
    g_file.resize((size_t)ftell(f));
    fseek(f, 0, SEEK_SET);
    if (fread(g_file.data(), 1, g_file.size(), f) != g_file.size()) { fclose(f); printf("cannot read %s\n", argv[1]); return 1; }
    fclose(f);
    const uint32_t pe = U32(0x3C);
    const uint16_t ns = (uint16_t)(U32(pe + 6) & 0xFFFF), opt = (uint16_t)(U32(pe + 20) & 0xFFFF);
    for (int i = 0; i < ns; ++i) {
        const size_t o = pe + 24 + opt + i * 40;
        g_secs.push_back({U32(o + 12), U32(o + 8), U32(o + 20), U32(o + 16)});
    }
    // Every sun shadow receiver in the file is one of the 24 expected ones.
    int found = 0;
    for (size_t i = 0; i + 8 <= g_file.size(); i += 4)
        if (U32(i) == 0xFFFF0300u && CtabNames(i, "CTAB") && CtabNames(i, "g_vCascade") && CtabNames(i, "g_tDepth2")) {
            const uint32_t va = VaOf(i);
            bool known = false;
            for (const Expect& e : kExpect) known = known || e.va == va;
            if (!known) { printf("FAIL: a sun shadow shader at %08X is not in the list\n", va); ++bad; }
            ++found;
        }
    if (found != 24) { printf("FAIL: %d sun shadow shaders in the file, 24 expected\n", found); ++bad; }
    int patched = 0;
    for (const Expect& e : kExpect) {
        const long off = OffsetOf(e.va);
        if (off < 0) { printf("FAIL: %08X is not in the file\n", e.va); ++bad; continue; }
        std::vector<uint32_t> code = Words(off);
        const int n = ufx::PatchFarCascade(code.data(), out, &why);
        if (n != (int)e.words || Fnv(out, n) != e.fnv) {
            printf("FAIL: %08X gave %d words (%s), fnv %08X; expected %u, %08X\n", e.va, n, why, n ? Fnv(out, n) : 0, e.words, e.fnv);
            ++bad;
            continue;
        }
        ++patched;
        // A damaged copy is kept as the game's: a near tap on another sampler, and c31 already used by the shader.
        for (size_t i = 1; i + 3 < code.size(); ++i)
            if (code[i] == ufx::kFarTexldp && code[i + 3] == ufx::kFarS4) {
                std::vector<uint32_t> d = code;
                d[i + 3] = 0xA0E40807u;
                if (ufx::PatchFarCascade(d.data(), out, &why)) { printf("FAIL: %08X with a changed near tap was patched\n", e.va); ++bad; }
                break;
            }
        for (size_t i = 1; i + 2 < code.size(); ++i)
            if (code[i] == 0x03000005u) {  // the first mul: one source becomes c31
                std::vector<uint32_t> d = code;
                d[i + 3] = 0xA0E4001Fu;
                if (ufx::PatchFarCascade(d.data(), out, &why)) { printf("FAIL: %08X reading c31 was patched\n", e.va); ++bad; }
                break;
            }
    }
    printf("%s (%d of 24 sun shadow shaders patched as expected)\n", bad ? "FARSHADOW TEST FAILED" : "farshadow test passed", patched);
    return bad ? 1 : 0;
}

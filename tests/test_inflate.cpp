// Host test of src/inflate.h: the game's keyboard icon atlas (an XPC2 with a zlib DDS) must decode exactly as zlib does.
#include "../src/inflate.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <zlib.h>
int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: test_inflate file.xpc\n"); return 2; }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { printf("cannot open %s\n", argv[1]); return 2; }
    std::vector<uint8_t> x; int c; while ((c = fgetc(f)) != EOF) x.push_back((uint8_t)c); fclose(f);
    size_t need = 0;
    ufx::XpcFirstPicture(x.data(), x.size(), nullptr, 0, &need);
    std::vector<uint8_t> a(need);
    size_t got = ufx::XpcFirstPicture(x.data(), x.size(), a.data(), a.size(), nullptr);
    const uint8_t* e = x.data() + 0x40;
    uint32_t off = e[0x10] | e[0x11] << 8 | e[0x12] << 16 | e[0x13] << 24, csz = e[0x14] | e[0x15] << 8 | e[0x16] << 16 | e[0x17] << 24;
    std::vector<uint8_t> b(need); uLongf bl = need;
    int z = uncompress(b.data(), &bl, x.data() + off, csz);
    bool same = got == need && z == Z_OK && bl == need && a == b;
    printf("inflate: %zu of %zu bytes, zlib %d, %s\n", got, need, z, same ? "identical" : "DIFFERENT");
    // a fixed-code and a stored stream too
    const char* text = "Greenvale Greenvale Greenvale, a quiet town where the rain never stops raining raining raining.";
    for (int level : {0, 1, 9}) {
        uLongf cl = 1024; std::vector<uint8_t> comp(cl);
        compress2(comp.data(), &cl, (const Bytef*)text, strlen(text), level);
        std::vector<uint8_t> o(200);
        size_t n = ufx::ZlibInflate(comp.data(), cl, o.data(), o.size());
        bool ok = n == strlen(text) && memcmp(o.data(), text, n) == 0;
        printf("level %d: %s\n", level, ok ? "ok" : "FAIL");
        same = same && ok;
    }
    return same ? 0 : 1;
}

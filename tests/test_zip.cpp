// Host test of the zip reader in src/inflate.h: every entry of a real release archive unpacks with a matching CRC.
#include "../src/inflate.h"
#include <cstdio>
#include <vector>
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    FILE* f = fopen(argv[1], "rb"); if (!f) return 2;
    std::vector<uint8_t> z; int c; while ((c = fgetc(f)) != EOF) z.push_back((uint8_t)c); fclose(f);
    int n = 0, bad = 0; size_t total = 0;
    bool ok = ufx::ZipForEach(z.data(), z.size(), [&](const ufx::ZipEntry& e) {
        std::vector<uint8_t> out(e.size + 1);
        if (e.size && !ufx::ZipExtract(e, out.data())) { ++bad; printf("BAD %.*s\n", e.nameLen, e.name); }
        ++n; total += e.size; return true; });
    printf("zip: %s, %d entries, %zu bytes, %d bad\n", ok ? "read" : "NOT READ", n, total, bad);
    return ok && !bad ? 0 : 1;
}

// Host test of the resource read guard's decisions (src/resguard.h).
//
// The unpack loop of the game (FUN_006AF5B0, written out below from its disassembly) is run against the host's zlib two
// ways: as the game does it (20000h bytes of output room, again whenever it runs out) and with the guard's room
// (never more than what is left of the buffer). For a stream that fits its buffer both must give the same bytes, the
// same result and read the same amount of the file; for a stream that holds more than the buffer the guarded loop must
// stop with a failure and leave the bytes behind the buffer alone. The game carries zlib 1.2.1 and the host a newer one:
// the test checks the arithmetic of the guard and the contract of inflate() it relies on, not the game's copy of zlib.
// Build:  make test
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <zlib.h>

#include "resguard.h"

using namespace ufx;

static int g_failures = 0;

static void Check(bool ok, const char* what, size_t a = 0, size_t b = 0) {
    if (!ok) {
        printf("FAIL  %s (%zu, %zu)\n", what, a, b);
        ++g_failures;
    }
}

constexpr uint32_t kStep = 0x20000;     // output room the game hands out
constexpr uint32_t kChunk = 0x10000;    // input it reads at a time
constexpr size_t kCanary = 0x40000;     // bytes behind the buffer that the guarded loop must not touch

struct Result {
    bool ok;
    size_t written;   // z_stream.total_out at the end
    size_t fileRead;  // bytes taken from the file
    int calls;        // inflate() calls
};

// FUN_006AF5B0(out, packed, file): `packed` bytes of `file` (which may be shorter: fread then returns less) are fed to
// inflate() in chunks of 10000h; 1 = Z_STREAM_END, 0 = any other error. `bufferSize` < 0 = the game's own room.
static Result Unpack(uint8_t* out, long bufferSize, const std::vector<uint8_t>& file, uint32_t packed) {
    Result r = {false, 0, 0, 0};
    z_stream s;
    memset(&s, 0, sizeof(s));
    if (inflateInit(&s) != Z_OK) return r;
    static uint8_t in[kChunk];
    s.avail_in = 0;
    s.avail_out = bufferSize < 0 ? kStep : InflateRoom((uint32_t)bufferSize, 0, kStep);
    s.next_in = nullptr;
    s.next_out = out;
    for (;;) {
        if (s.avail_in == 0) {
            s.next_in = in;
            uint32_t want = packed < kChunk ? packed : kChunk;
            size_t have = file.size() - r.fileRead;
            uint32_t got = want < have ? want : (uint32_t)have;
            memcpy(in, file.data() + r.fileRead, got);
            r.fileRead += got;
            s.avail_in = got;
            packed -= got;
        }
        int ret = inflate(&s, Z_NO_FLUSH);
        ++r.calls;
        if (r.calls > 100000) break;  // a loop that never ends would be the bug this guard exists for
        if (ret == Z_STREAM_END) {
            r.ok = true;
            break;
        }
        if (ret != Z_OK) break;
        if (s.avail_out == 0) s.avail_out = bufferSize < 0 ? kStep : InflateRoom((uint32_t)bufferSize, (uint32_t)s.total_out, kStep);
    }
    r.written = s.total_out;
    inflateEnd(&s);
    return r;
}

static std::vector<uint8_t> MakeData(size_t n, bool compressible, uint32_t seed) {
    std::vector<uint8_t> d(n);
    uint32_t x = seed * 2654435761u + 12345;
    for (size_t i = 0; i < n; ++i) {
        x = x * 1664525u + 1013904223u;
        d[i] = compressible ? (uint8_t)((i / 97) ^ ((x >> 28) & 1)) : (uint8_t)(x >> 24);
    }
    return d;
}

static std::vector<uint8_t> Pack(const std::vector<uint8_t>& d, int level) {
    uLongf n = compressBound((uLong)d.size());
    std::vector<uint8_t> z(n);
    compress2(z.data(), &n, d.data(), (uLong)d.size(), level);
    z.resize(n);
    return z;
}

// One stream of `n` bytes: the game's loop and the guarded loop on a buffer of exactly n bytes, then on smaller buffers.
static void OneStream(size_t n, bool compressible, int level, uint32_t seed) {
    const std::vector<uint8_t> data = MakeData(n, compressible, seed);
    const std::vector<uint8_t> z = Pack(data, level);
    std::vector<uint8_t> plain(n + kStep + kCanary, 0xEE), guarded(n + kCanary, 0xEE);

    Result a = Unpack(plain.data(), -1, z, (uint32_t)z.size());
    Result b = Unpack(guarded.data(), (long)n, z, (uint32_t)z.size());
    Check(a.ok && a.written == n, "the game's loop unpacks the stream", n, a.written);
    Check(b.ok, "guarded loop: same result", n, z.size());
    Check(b.written == a.written && b.fileRead == a.fileRead, "guarded loop: same size and file position", b.written, b.fileRead);
    Check(n == 0 || memcmp(guarded.data(), data.data(), n) == 0, "guarded loop: same bytes", n);
    bool clean = true;
    for (size_t i = n; i < guarded.size(); ++i) clean &= guarded[i] == 0xEE;
    Check(clean, "guarded loop: nothing behind the buffer", n);

    // The header declares less than the stream holds: the game's loop writes past the buffer, the guarded one stops.
    const size_t cuts[4] = {1, 258, kStep, n / 2 + 1};
    for (size_t cut : cuts) {
        if (cut > n) continue;
        const size_t declared = n - cut;
        std::vector<uint8_t> small(declared + kCanary, 0xEE);
        Result c = Unpack(small.data(), (long)declared, z, (uint32_t)z.size());
        Check(!c.ok, "oversized stream: the guarded loop fails", n, cut);
        Check(c.written <= declared, "oversized stream: not more than the buffer is written", c.written, declared);
        bool intact = true;
        for (size_t i = declared; i < small.size(); ++i) intact &= small[i] == 0xEE;
        Check(intact, "oversized stream: nothing behind the buffer", n, cut);
        Check(declared == 0 || memcmp(small.data(), data.data(), c.written) == 0, "oversized stream: what was written is right", n, cut);
    }

    // A file that ends early (a short read): both loops fail, neither hangs.
    if (z.size() > 8) {
        std::vector<uint8_t> shortFile(z.begin(), z.begin() + (long)(z.size() - 3));
        Result d = Unpack(plain.data(), -1, shortFile, (uint32_t)z.size());
        Result e = Unpack(guarded.data(), (long)n, shortFile, (uint32_t)z.size());
        Check(!d.ok && !e.ok, "truncated file: both loops fail", n);
        Check(d.fileRead == e.fileRead, "truncated file: same file position", d.fileRead, e.fileRead);
    }
}

static void TestRoom() {
    Check(InflateRoom(0x50000, 0, kStep) == kStep, "room: a big buffer gets the game's step");
    Check(InflateRoom(0x50000, 0x40000, kStep) == 0x10000, "room: the rest of the buffer");
    Check(InflateRoom(0x50000, 0x50000, kStep) == 0, "room: a full buffer gets none");
    Check(InflateRoom(0x50000, 0x50001, kStep) == 0, "room: never negative");
    Check(InflateRoom(100, 0, kStep) == 100, "room: a small buffer gets its size");
    Check(InflateRoom(0, 0, kStep) == 0, "room: an empty buffer gets none");
    Check(InflateRoom(0xFFFFFFFFu, 0xFFFF0000u, kStep) == 0xFFFF, "room: no overflow near 4 GB");

    static const size_t sizes[] = {0, 1, 2, 257, 258, 259, 4095, 65535, 65536, 65537, kStep - 1, kStep, kStep + 1, kStep + 257, kStep + 258,
                                   2 * kStep - 1, 2 * kStep, 2 * kStep + 1, 300000, 566464, 1048576 + 77};
    int streams = 0;
    for (size_t n : sizes)
        for (int compressible = 0; compressible < 2; ++compressible)
            for (int level : {0, 1, 9}) {
                OneStream(n, compressible != 0, level, (uint32_t)(n * 3 + (size_t)level));
                ++streams;
            }
    // Stored streams whose packed size ends 1..8 bytes behind a 10000h input chunk: the last output byte and the Adler-32
    // trailer then arrive in different inflate() calls, the second one with no output room at all in the guarded loop.
    for (size_t n = 2 * kChunk - 40; n < 2 * kChunk + 8; ++n) {
        const std::vector<uint8_t> z = Pack(MakeData(n, false, 7), 0);
        const size_t tail = z.size() % kChunk;
        if (tail >= 1 && tail <= 8) {
            OneStream(n, false, 0, 7);
            ++streams;
        }
    }
    printf("inflate room: %d streams compared\n", streams);
}

static void TestRetry() {
    const uint32_t kMax = 5;
    RetryState s;
    memset(&s, 0, sizeof(s));
    // A record that never unpacks: four more reads, then given up; afterwards one failed read is enough.
    int again = 0;
    RetryStep step;
    while ((step = NextRetryStep(s, 100, kMax)) == kReadAgain) ++again;
    Check(again == 4 && step == kGiveUp, "retry: 5 reads, then give up", (size_t)again);
    Check(NextRetryStep(s, 100, kMax) == kGiveUpKnown, "retry: a known bad record is given up at once");
    Check(NextRetryStep(s, 100, kMax) == kGiveUpKnown, "retry: and again");
    // Another record starts its own count.
    Check(NextRetryStep(s, 200, kMax) == kReadAgain, "retry: a new record is read again");
    Check(NextRetryStep(s, 300, kMax) == kReadAgain, "retry: a different record resets the run");
    again = 1;
    while ((step = NextRetryStep(s, 300, kMax)) == kReadAgain) ++again;
    Check(again == 4 && step == kGiveUp, "retry: the run of the new record is 5 reads too", (size_t)again);
    // No sequence of failures of one record is longer than kMax reads, whatever came before.
    memset(&s, 0, sizeof(s));
    for (uint32_t key = 1; key <= 40; ++key) {
        int reads = 1;
        while (NextRetryStep(s, key, kMax) == kReadAgain) ++reads;
        Check(reads <= (int)kMax, "retry: never more than 5 reads in a row", key, (size_t)reads);
    }
    // The table of known bad records keeps the newest 16: an older one gets a full run again, still bounded.
    Check(NextRetryStep(s, 40, kMax) == kGiveUpKnown, "retry: the newest bad record is known");
    Check(NextRetryStep(s, 1, kMax) == kReadAgain, "retry: a forgotten one is retried");
    printf("retry limit: checked\n");
}

int main() {
    TestRoom();
    TestRetry();
    printf("%s\n", g_failures ? "RESOURCE GUARD CHECKS FAILED" : "all resource guard checks passed");
    return g_failures ? 1 : 0;
}

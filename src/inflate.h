// A small zlib / DEFLATE decoder (RFC 1950/1951), freestanding: stored, fixed and dynamic Huffman blocks. Used to read the
// game's own XPC pictures (the pad icon atlas). No Windows headers here, so the host tests run it against zlib.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace ufx {

struct InflateState {
    const uint8_t* src;
    size_t srcLen, pos;
    uint32_t bitBuf;
    int bitCount;
    uint8_t* dst;
    size_t dstLen, out;
    bool bad;
};

struct Huffman {
    uint16_t counts[16];
    uint16_t symbols[288];
};

inline int InflateBit(InflateState& s) {
    if (s.bitCount == 0) {
        if (s.pos >= s.srcLen) { s.bad = true; return 0; }
        s.bitBuf = s.src[s.pos++];
        s.bitCount = 8;
    }
    const int b = (int)(s.bitBuf & 1);
    s.bitBuf >>= 1;
    --s.bitCount;
    return b;
}

inline uint32_t InflateBits(InflateState& s, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; ++i) v |= (uint32_t)InflateBit(s) << i;
    return v;
}

inline void HuffmanBuild(Huffman& h, const uint8_t* lengths, int n) {
    for (int i = 0; i < 16; ++i) h.counts[i] = 0;
    for (int i = 0; i < n; ++i) h.counts[lengths[i]]++;
    h.counts[0] = 0;
    uint16_t offs[16];
    offs[1] = 0;
    for (int i = 1; i < 15; ++i) offs[i + 1] = (uint16_t)(offs[i] + h.counts[i]);
    for (int i = 0; i < n; ++i)
        if (lengths[i]) h.symbols[offs[lengths[i]]++] = (uint16_t)i;
}

inline int HuffmanDecode(InflateState& s, const Huffman& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; ++len) {
        code |= InflateBit(s);
        const int count = h.counts[len];
        if (code - count < first) return h.symbols[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
        if (s.bad) return -1;
    }
    s.bad = true;
    return -1;
}

inline bool InflateCodes(InflateState& s, const Huffman& lit, const Huffman& dist) {
    static const uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
                                           4097, 6145, 8193, 12289, 16385, 24577};
    static const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    for (;;) {
        int sym = HuffmanDecode(s, lit);
        if (s.bad || sym < 0) return false;
        if (sym < 256) {
            if (s.out >= s.dstLen) return false;
            s.dst[s.out++] = (uint8_t)sym;
        } else if (sym == 256) {
            return true;
        } else {
            sym -= 257;
            if (sym >= 29) return false;
            const size_t len = kLenBase[sym] + InflateBits(s, kLenExtra[sym]);
            const int ds = HuffmanDecode(s, dist);
            if (s.bad || ds < 0 || ds >= 30) return false;
            const size_t d = kDistBase[ds] + InflateBits(s, kDistExtra[ds]);
            if (d > s.out || s.out + len > s.dstLen) return false;
            for (size_t i = 0; i < len; ++i, ++s.out) s.dst[s.out] = s.dst[s.out - d];
        }
    }
}

// Inflates a zlib stream (2-byte header) into dst; returns the bytes written, 0 on any error.
inline size_t ZlibInflate(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen) {
    if (srcLen < 2 || (src[0] & 0x0F) != 8 || ((src[0] << 8) | src[1]) % 31 != 0 || (src[1] & 0x20)) return 0;
    InflateState s = {src, srcLen, 2, 0, 0, dst, dstLen, 0, false};
    static Huffman lit, dist;  // big for the stack of a game thread
    int last = 0;
    do {
        last = InflateBit(s);
        const uint32_t type = InflateBits(s, 2);
        if (s.bad) return 0;
        if (type == 0) {  // stored
            s.bitCount = 0;
            if (s.pos + 4 > s.srcLen) return 0;
            const uint32_t len = (uint32_t)s.src[s.pos] | ((uint32_t)s.src[s.pos + 1] << 8);
            const uint32_t nlen = (uint32_t)s.src[s.pos + 2] | ((uint32_t)s.src[s.pos + 3] << 8);
            s.pos += 4;
            if ((len ^ 0xFFFF) != nlen || s.pos + len > s.srcLen || s.out + len > s.dstLen) return 0;
            for (uint32_t i = 0; i < len; ++i) s.dst[s.out++] = s.src[s.pos++];
        } else if (type == 1) {  // fixed codes
            uint8_t l[288];
            int i = 0;
            for (; i < 144; ++i) l[i] = 8;
            for (; i < 256; ++i) l[i] = 9;
            for (; i < 280; ++i) l[i] = 7;
            for (; i < 288; ++i) l[i] = 8;
            HuffmanBuild(lit, l, 288);
            for (i = 0; i < 30; ++i) l[i] = 5;
            HuffmanBuild(dist, l, 30);
            if (!InflateCodes(s, lit, dist)) return 0;
        } else if (type == 2) {  // dynamic codes
            const int nlit = (int)InflateBits(s, 5) + 257, ndist = (int)InflateBits(s, 5) + 1, nclen = (int)InflateBits(s, 4) + 4;
            if (nlit > 286 || ndist > 30) return 0;
            static const uint8_t kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            uint8_t lengths[320] = {};
            for (int i = 0; i < nclen; ++i) lengths[kOrder[i]] = (uint8_t)InflateBits(s, 3);
            Huffman clen;
            HuffmanBuild(clen, lengths, 19);
            int n = 0;
            while (n < nlit + ndist) {
                const int sym = HuffmanDecode(s, clen);
                if (s.bad || sym < 0) return 0;
                if (sym < 16) {
                    lengths[n++] = (uint8_t)sym;
                } else {
                    int rep = 0;
                    uint8_t val = 0;
                    if (sym == 16) {
                        if (n == 0) return 0;
                        val = lengths[n - 1];
                        rep = 3 + (int)InflateBits(s, 2);
                    } else if (sym == 17) {
                        rep = 3 + (int)InflateBits(s, 3);
                    } else {
                        rep = 11 + (int)InflateBits(s, 7);
                    }
                    if (n + rep > nlit + ndist) return 0;
                    while (rep--) lengths[n++] = val;
                }
            }
            HuffmanBuild(lit, lengths, nlit);
            HuffmanBuild(dist, lengths + nlit, ndist);
            if (!InflateCodes(s, lit, dist)) return 0;
        } else {
            return 0;
        }
    } while (!last);
    return s.out;
}

// The first picture of an XPC2 container (the game's picture format): entry 0 at +40h, +10h offset, +14h packed size,
// +1Dh the 24-bit unpacked size; the data is a zlib stream of a DDS file. Returns the DDS size written into dst, 0 on error.
inline size_t XpcFirstPicture(const uint8_t* xpc, size_t len, uint8_t* dst, size_t dstLen, size_t* needed) {
    if (len < 0x60 || xpc[0] != 'X' || xpc[1] != 'P' || xpc[2] != 'C' || xpc[3] != '2') return 0;
    const uint8_t* e = xpc + 0x40;
    const uint32_t off = (uint32_t)e[0x10] | ((uint32_t)e[0x11] << 8) | ((uint32_t)e[0x12] << 16) | ((uint32_t)e[0x13] << 24);
    const uint32_t csz = (uint32_t)e[0x14] | ((uint32_t)e[0x15] << 8) | ((uint32_t)e[0x16] << 16) | ((uint32_t)e[0x17] << 24);
    const uint32_t unc = (uint32_t)e[0x1D] | ((uint32_t)e[0x1E] << 8) | ((uint32_t)e[0x1F] << 16);
    if (needed) *needed = unc;
    if (!dst) return 0;
    if (off >= len || csz > len - off || unc > dstLen || unc < 128) return 0;
    const size_t got = ZlibInflate(xpc + off, csz, dst, unc);
    return got == unc && dst[0] == 'D' && dst[1] == 'D' && dst[2] == 'S' ? got : 0;
}

}  // namespace ufx

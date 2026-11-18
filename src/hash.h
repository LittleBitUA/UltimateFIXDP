// SuperFastHash by Paul Hsieh (public algorithm, http://www.azillionmonkeys.com/qed/hash.html), written out here.
// Used for the names of texture replacement files. No Windows headers: tested on the host.
#pragma once
#include <stdint.h>

namespace ufx {

inline uint32_t Get16(const uint8_t* d) { return (uint32_t)d[0] | ((uint32_t)d[1] << 8); }

// SuperFastHash by Paul Hsieh (http://www.azillionmonkeys.com/qed/hash.html, derived work license: LGPL 2.1 / BSD).
inline uint32_t SuperFastHash(const uint8_t* data, int len) {
    if (len <= 0 || !data) return 0;
    uint32_t hash = (uint32_t)len, tmp;
    int rem = len & 3;
    len >>= 2;
    for (; len > 0; --len) {
        hash += Get16(data);
        tmp = (Get16(data + 2) << 11) ^ hash;
        hash = (hash << 16) ^ tmp;
        data += 4;
        hash += hash >> 11;
    }
    switch (rem) {
        case 3:
            hash += Get16(data);
            hash ^= hash << 16;
            hash ^= (uint32_t)((int)(signed char)data[2] << 18);
            hash += hash >> 11;
            break;
        case 2:
            hash += Get16(data);
            hash ^= hash << 11;
            hash += hash >> 17;
            break;
        case 1:
            hash += (uint32_t)(int)(signed char)*data;
            hash ^= hash << 10;
            hash += hash >> 1;
    }
    hash ^= hash << 3;
    hash += hash >> 5;
    hash ^= hash << 4;
    hash += hash >> 17;
    hash ^= hash << 25;
    hash += hash >> 6;
    return hash;
}


}  // namespace ufx

// Compares our SuperFastHash with the reference implementation (as published in DPfix's Hash.h) on random data.
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "hash.h"
typedef uint32_t UINT32; typedef uint8_t UINT8; typedef uint16_t UINT16;
#define get16bits(d) ((((UINT32)(((const UINT8 *)(d))[1])) << 8)+(UINT32)(((const UINT8 *)(d))[0]) )
static UINT32 Ref(const char* data, int len) {
    UINT32 hash = len, tmp; int rem;
    if (len <= 0 || data == NULL) return 0;
    rem = len & 3; len >>= 2;
    for (; len > 0; len--) { hash += get16bits(data); tmp = (get16bits(data + 2) << 11) ^ hash; hash = (hash << 16) ^ tmp; data += 2 * sizeof(UINT16); hash += hash >> 11; }
    switch (rem) {
        case 3: hash += get16bits(data); hash ^= hash << 16; hash ^= ((signed char)data[sizeof(UINT16)]) << 18; hash += hash >> 11; break;
        case 2: hash += get16bits(data); hash ^= hash << 11; hash += hash >> 17; break;
        case 1: hash += (signed char)*data; hash ^= hash << 10; hash += hash >> 1;
    }
    hash ^= hash << 3; hash += hash >> 5; hash ^= hash << 4; hash += hash >> 17; hash ^= hash << 25; hash += hash >> 6;
    return hash;
}
int main() {
    srand(7);
    uint8_t buf[4100];
    int bad = 0;
    for (int n = 0; n < 4000; ++n) {
        int len = n % 4099 + 1;
        for (int i = 0; i < len; ++i) buf[i] = (uint8_t)rand();
        if (ufx::SuperFastHash(buf, len) != Ref((const char*)buf, len)) ++bad;
    }
    printf("%s\n", bad ? "HASH TEST FAILED" : "HASH TEST PASSED");
    return bad ? 1 : 0;
}

// The few C library functions the compiler may emit calls to. No C runtime is linked.
#include <stddef.h>

#define NO_LOOP_PATTERNS __attribute__((optimize("no-tree-loop-distribute-patterns")))

extern "C" {

NO_LOOP_PATTERNS void* memcpy(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    while (n--) *d++ = *s++;
    return dst;
}

NO_LOOP_PATTERNS void* memmove(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

NO_LOOP_PATTERNS void* memset(void* dst, int c, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}

NO_LOOP_PATTERNS int memcmp(const void* a, const void* b, size_t n) {
    const unsigned char* x = (const unsigned char*)a;
    const unsigned char* y = (const unsigned char*)b;
    for (; n; --n, ++x, ++y)
        if (*x != *y) return *x < *y ? -1 : 1;
    return 0;
}

}  // extern "C"

// The decisions of the resource read guard (modules/resource_guard.cpp), kept apart from the hooks so that the host
// can test them (tests/test_resguard.cpp). No Windows headers.
#pragma once
#include <stdint.h>

namespace ufx {

// How much output room inflate() gets next: the game's own step, but never more than what is left of a buffer of
// `bufferSize` bytes into which the stream has written `written` bytes so far.
inline uint32_t InflateRoom(uint32_t bufferSize, uint32_t written, uint32_t step) {
    const uint32_t left = written < bufferSize ? bufferSize - written : 0;
    return left < step ? left : step;
}

constexpr int kRetryKnownBad = 16;

struct RetryState {
    uint32_t key;                  // the record of the current run of failures (0 = none; a key is never 0)
    uint32_t fails;                // failed reads of it in a row
    uint32_t bad[kRetryKnownBad];  // records given up on (the newest 16)
    uint32_t badCount;
};

enum RetryStep {
    kReadAgain,    // the game's own jump: read the record once more
    kGiveUp,       // the last attempt failed: report the record missing
    kGiveUpKnown,  // a record already given up on failed again: report it missing at once
};

// One failed unpack of the record `key`. A run of failures is counted per record: `maxAttempts` reads, then kGiveUp.
// A record given up on is remembered, so that a resource the game keeps asking for costs one failed read each time and
// not the whole run again. A success in between is not seen (no code of the guard runs on that path): a record that
// failed, then unpacked, then fails again goes on with the old count, which means fewer attempts, never more.
inline RetryStep NextRetryStep(RetryState& s, uint32_t key, uint32_t maxAttempts) {
    for (int i = 0; i < kRetryKnownBad; ++i)
        if (s.bad[i] == key) return kGiveUpKnown;
    if (s.key != key) {
        s.key = key;
        s.fails = 0;
    }
    if (++s.fails < maxAttempts) return kReadAgain;
    s.key = 0;
    s.fails = 0;
    s.bad[s.badCount++ % kRetryKnownBad] = key;
    return kGiveUp;
}

}  // namespace ufx

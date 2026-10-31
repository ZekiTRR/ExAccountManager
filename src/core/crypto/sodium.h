#pragma once

#include <sodium.h>

namespace ex::crypto {

// libsodium initialization (idempotent; every crypto module calls it).
inline bool ensureSodium()
{
    static bool ok = sodium_init() >= 0;
    return ok;
}

// Generates cryptographically strong random bytes.
inline void randomBytes(unsigned char* out, size_t len)
{
    ensureSodium();
    randombytes_buf(out, len);
}

} // namespace ex::crypto

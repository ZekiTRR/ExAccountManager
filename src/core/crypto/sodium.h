#pragma once

#include <sodium.h>

namespace ex::crypto {

// Инициализация libsodium (идемпотентная, вызывается всеми модулями).
inline bool ensureSodium()
{
    static bool ok = sodium_init() >= 0;
    return ok;
}

// Генерация криптостойких случайных байт.
inline void randomBytes(unsigned char* out, size_t len)
{
    ensureSodium();
    randombytes_buf(out, len);
}

} // namespace ex::crypto

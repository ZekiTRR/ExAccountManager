#include "platform/windows/dpapi.h"

#include "core/util/log.h"

#include <windows.h>
#include <wincrypt.h>

#pragma comment(lib, "crypt32.lib")

namespace ex::platform::dpapi {

QByteArray protect(const QByteArray& plain, bool* ok)
{
    DATA_BLOB in{};
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plain.constData()));
    in.cbData = static_cast<DWORD>(plain.size());
    DATA_BLOB out{};
    // CRYPTPROTECT_UI_FORBIDDEN — без UI-промптов (принцип Astro)
    if (!CryptProtectData(&in, L"ExAccountManager", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        if (ok) *ok = false;
        return {};
    }
    QByteArray result(reinterpret_cast<const char*>(out.pbData),
                      static_cast<qsizetype>(out.cbData));
    LocalFree(out.pbData);
    if (ok) *ok = true;
    return result;
}

QByteArray unprotect(const QByteArray& blob, bool* ok)
{
    DATA_BLOB in{};
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(blob.constData()));
    in.cbData = static_cast<DWORD>(blob.size());
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        if (ok) *ok = false;
        return {};
    }
    QByteArray result(reinterpret_cast<const char*>(out.pbData),
                      static_cast<qsizetype>(out.cbData));
    LocalFree(out.pbData);
    if (ok) *ok = true;
    return result;
}

} // namespace ex::platform::dpapi

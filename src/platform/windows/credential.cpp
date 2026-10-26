#include "platform/windows/credential.h"

#include "core/util/log.h"

#include <windows.h>
#include <wincred.h>

#include <array>

namespace ex::platform::cred {

bool write(const QString& service, const QString& account, const QByteArray& secret)
{
    // Ограничение CRED_MAX_CREDENTIAL_BLOB_SIZE — 5*512 = 2560 байт; наши ключи 32 байта.
    if (secret.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
        EXLOG_WARN("cred") << "secret too large for credential manager";
        return false;
    }
    std::array<wchar_t, 512> target{};
    const QString full = service + QStringLiteral("/") + account;
    if (full.toWCharArray(target.data()) >= static_cast<int>(target.size()))
        return false;

    CREDENTIALW cred{};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = target.data();
    cred.UserName = const_cast<wchar_t*>(L"ExAccountManager");
    cred.CredentialBlobSize = static_cast<DWORD>(secret.size());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(secret.constData()));
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;

    if (!CredWriteW(&cred, 0)) {
        EXLOG_WARN("cred") << "CredWriteW failed:" << GetLastError();
        return false;
    }
    return true;
}

bool read(const QString& service, const QString& account, QByteArray& out)
{
    std::array<wchar_t, 512> target{};
    const QString full = service + QStringLiteral("/") + account;
    if (full.toWCharArray(target.data()) >= static_cast<int>(target.size()))
        return false;

    PCREDENTIALW cred = nullptr;
    if (!CredReadW(target.data(), CRED_TYPE_GENERIC, 0, &cred)) {
        return false; // чаще всего ERROR_NOT_FOUND — штатная ситуация
    }
    out = QByteArray(reinterpret_cast<const char*>(cred->CredentialBlob),
                     static_cast<qsizetype>(cred->CredentialBlobSize));
    CredFree(cred);
    return true;
}

bool remove(const QString& service, const QString& account)
{
    std::array<wchar_t, 512> target{};
    const QString full = service + QStringLiteral("/") + account;
    if (full.toWCharArray(target.data()) >= static_cast<int>(target.size()))
        return false;
    return CredDeleteW(target.data(), CRED_TYPE_GENERIC, 0);
}

} // namespace ex::platform::cred

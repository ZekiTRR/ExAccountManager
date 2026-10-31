#pragma once

#include <QByteArray>

namespace ex::platform::dpapi {

// Windows DPAPI (CurrentUser): the blob is encrypted so only the same
// same user on the same machine — regardless of elevation
// and the logon session. That is how the C# RAM guards AccountData.json.
QByteArray protect(const QByteArray& plain, bool* ok = nullptr);
QByteArray unprotect(const QByteArray& blob, bool* ok = nullptr);

} // namespace ex::platform::dpapi

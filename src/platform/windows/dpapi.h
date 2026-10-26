#pragma once

#include <QByteArray>

namespace ex::platform::dpapi {

// Windows DPAPI (CurrentUser): шифрование блоба, расшифровать может только
// тот же пользователь на той же машине — независимо от прав (elevation)
// и logon-сессии. Именно так C# RAM защищает AccountData.json.
QByteArray protect(const QByteArray& plain, bool* ok = nullptr);
QByteArray unprotect(const QByteArray& blob, bool* ok = nullptr);

} // namespace ex::platform::dpapi

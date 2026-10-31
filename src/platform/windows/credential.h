#pragma once

#include <QString>
#include <QByteArray>

namespace ex::platform::cred {

// Windows Credential Manager (ordinary CurrentUser credentials).
bool write(const QString& service, const QString& account, const QByteArray& secret);
bool read(const QString& service, const QString& account, QByteArray& out);
bool remove(const QString& service, const QString& account);

} // namespace ex::platform::cred

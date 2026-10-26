#pragma once

#include "core/store/database.h"

#include <QByteArray>
#include <QString>

namespace ex::store {

// Секреты аккаунтов (cookie, пароли) — AES-256-GCM под data key из vault.dat.
// Ключи шифрования не хранятся в БД; без разблокированного vault — мёртвые блобы.
class Vault {
public:
    Vault(Database& db) : m_db(db) {}

    void setDataKey(const QByteArray& dataKey) { m_dataKey = dataKey; }
    bool isUnlocked() const { return !m_dataKey.isEmpty(); }

    bool setCookie(qint64 accountId, const QString& cookie);
    QString cookie(qint64 accountId) const;

    bool setPassword(qint64 accountId, const QString& password);
    QString password(qint64 accountId) const;

    bool clearAccount(qint64 accountId);
    // Удалить все зашифрованные секреты (после пересоздания vault они мертвы).
    bool clearAll();

private:
    QByteArray seal(const QString& plain, qint64 accountId, const char* kind) const;
    QString open(const QByteArray& sealed, qint64 accountId, const char* kind) const;

    Database& m_db;
    QByteArray m_dataKey;
};

} // namespace ex::store

#pragma once

#include "core/store/database.h"

#include <QByteArray>
#include <QString>

namespace ex::store {

// Account secrets (cookies, passwords) — AES-256-GCM under the vault.dat key.
// Encryption keys never sit in the DB; without an unlocked vault blobs are dead.
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
    // Delete all encrypted secrets (after a vault recreate they are dead).
    bool clearAll();

private:
    QByteArray seal(const QString& plain, qint64 accountId, const char* kind) const;
    QString open(const QByteArray& sealed, qint64 accountId, const char* kind) const;

    Database& m_db;
    QByteArray m_dataKey;
};

} // namespace ex::store

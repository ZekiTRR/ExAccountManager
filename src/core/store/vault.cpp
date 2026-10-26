#include "core/store/vault.h"

#include "core/crypto/envelope.h"
#include "core/util/log.h"

namespace ex::store {

namespace {
// AAD связывает блоб с владельцем и типом секрета: блоб нельзя перенести
// на другой аккаунт или выдать за другой тип секрета.
QByteArray aadFor(qint64 accountId, const char* kind)
{
    return QByteArrayLiteral("exvault:") + QByteArray::number(accountId)
         + QByteArrayLiteral(":") + QByteArray(kind);
}
} // namespace

QByteArray Vault::seal(const QString& plain, qint64 accountId, const char* kind) const
{
    if (!isUnlocked())
        return {};
    return crypto::VaultKey::sealSecret(m_dataKey, aadFor(accountId, kind), plain.toUtf8());
}

QString Vault::open(const QByteArray& sealed, qint64 accountId, const char* kind) const
{
    if (!isUnlocked() || sealed.isEmpty())
        return {};
    bool ok = false;
    const QByteArray plain = crypto::VaultKey::openSecret(m_dataKey, aadFor(accountId, kind), sealed, &ok);
    return ok ? QString::fromUtf8(plain) : QString();
}

bool Vault::setCookie(qint64 accountId, const QString& cookie)
{
    const QByteArray sealed = seal(cookie, accountId, "cookie");
    if (sealed.isEmpty())
        return false;
    Stmt s(m_db.handle(), QStringLiteral(
        "INSERT INTO secret_entries (account_id, cookie) VALUES (?,?) "
        "ON CONFLICT(account_id) DO UPDATE SET cookie=excluded.cookie"));
    s.bind(1, accountId);
    s.bind(2, sealed);
    return s.exec();
}

QString Vault::cookie(qint64 accountId) const
{
    Stmt s(m_db.handle(), QStringLiteral("SELECT cookie FROM secret_entries WHERE account_id=?"));
    s.bind(1, accountId);
    if (s.step() && !s.colIsNull(0))
        return open(s.colBlob(0), accountId, "cookie");
    return {};
}

bool Vault::setPassword(qint64 accountId, const QString& password)
{
    const QByteArray sealed = seal(password, accountId, "password");
    if (sealed.isEmpty())
        return false;
    Stmt s(m_db.handle(), QStringLiteral(
        "INSERT INTO secret_entries (account_id, password) VALUES (?,?) "
        "ON CONFLICT(account_id) DO UPDATE SET password=excluded.password"));
    s.bind(1, accountId);
    s.bind(2, sealed);
    return s.exec();
}

QString Vault::password(qint64 accountId) const
{
    Stmt s(m_db.handle(), QStringLiteral("SELECT password FROM secret_entries WHERE account_id=?"));
    s.bind(1, accountId);
    if (s.step() && !s.colIsNull(0))
        return open(s.colBlob(0), accountId, "password");
    return {};
}

bool Vault::clearAccount(qint64 accountId)
{
    Stmt s(m_db.handle(), QStringLiteral("DELETE FROM secret_entries WHERE account_id=?"));
    s.bind(1, accountId);
    return s.exec();
}

bool Vault::clearAll()
{
    return m_db.exec(QStringLiteral("DELETE FROM secret_entries"));
}

} // namespace ex::store

#include "core/store/account_store.h"

#include "core/util/log.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>

namespace ex::store {

namespace {
QString optionalIso(const QDateTime& dt) { return dt.isValid() ? dt.toString(Qt::ISODate) : QString(); }

void bindAccount(Stmt& s, const model::Account& a, bool withId)
{
    if (withId)
        s.bind(1, a.id);
    const int off = withId ? 1 : 0;
    s.bind(off + 1, a.userId);
    s.bind(off + 2, a.username);
    s.bind(off + 3, a.displayName);
    s.bind(off + 4, a.alias);
    s.bind(off + 5, a.description);
    a.groupId ? s.bind(off + 6, a.groupId) : s.bindNull(off + 6);
    s.bind(off + 7, a.avatarUrl);
    s.bind(off + 8, a.favorite);
    s.bind(off + 9, a.sortOrder);
    s.bind(off + 10, a.priority);
    s.bind(off + 11, QJsonDocument(QJsonArray::fromStringList(a.tags)).toJson(QJsonDocument::Compact));
    s.bind(off + 12, QJsonDocument(QJsonObject::fromVariantHash(a.fields)).toJson(QJsonDocument::Compact));
    s.bind(off + 13, a.hasSession);
    s.bind(off + 14, a.cookieExpired);
    s.bind(off + 15, a.verified);
    if (a.moderation.moderated) {
        QJsonObject m;
        m[QStringLiteral("reason")] = a.moderation.reason;
        if (a.moderation.endTime.isValid())
            m[QStringLiteral("end")] = a.moderation.endTime.toString(Qt::ISODate);
        m[QStringLiteral("duration")] = static_cast<double>(a.moderation.durationSeconds);
        s.bind(off + 16, QJsonDocument(m).toJson(QJsonDocument::Compact));
    } else {
        s.bindNull(off + 16);
    }
    const QString lu = optionalIso(a.lastUsed);
    lu.isEmpty() ? s.bindNull(off + 17) : s.bind(off + 17, lu);
    const QString lv = optionalIso(a.lastValidated);
    lv.isEmpty() ? s.bindNull(off + 18) : s.bind(off + 18, lv);
    s.bind(off + 19, a.savedPlaceId);
    s.bind(off + 20, a.savedJobId);
    s.bind(off + 21, a.savedPrivateServerCode);
}

QString kAccountCols =
    QStringLiteral("id, user_id, username, display_name, alias, description, group_id, avatar_url, "
                   "favorite, sort_order, priority, tags, fields, has_session, cookie_expired, verified, "
                   "moderation, last_used, last_validated, saved_place_id, saved_job_id, saved_private_server_code");
} // namespace

model::Account AccountStore::accountFromRow(Stmt& s)
{
    model::Account a;
    a.id = s.colInt(0);
    a.userId = s.colInt(1);
    a.username = s.colText(2);
    a.displayName = s.colText(3);
    a.alias = s.colText(4);
    a.description = s.colText(5);
    a.groupId = s.colIsNull(6) ? 0 : s.colInt(6);
    a.avatarUrl = s.colText(7);
    a.favorite = s.colBool(8);
    a.sortOrder = static_cast<int>(s.colInt(9));
    a.priority = static_cast<int>(s.colInt(10));
    const QJsonArray tags = QJsonDocument::fromJson(s.colBlob(11)).array();
    for (const auto& v : tags)
        a.tags.append(v.toString());
    a.fields = QJsonDocument::fromJson(s.colBlob(12)).object().toVariantHash();
    a.hasSession = s.colBool(13);
    a.cookieExpired = s.colBool(14);
    a.verified = s.colBool(15);
    if (!s.colIsNull(16)) {
        const QJsonObject m = QJsonDocument::fromJson(s.colBlob(16)).object();
        a.moderation.moderated = true;
        a.moderation.reason = m[QStringLiteral("reason")].toString();
        a.moderation.endTime = QDateTime::fromString(m[QStringLiteral("end")].toString(), Qt::ISODate);
        a.moderation.durationSeconds = static_cast<qint64>(m[QStringLiteral("duration")].toDouble(0));
    }
    if (!s.colIsNull(17))
        a.lastUsed = QDateTime::fromString(s.colText(17), Qt::ISODate);
    if (!s.colIsNull(18))
        a.lastValidated = QDateTime::fromString(s.colText(18), Qt::ISODate);
    a.savedPlaceId = s.colInt(19);
    a.savedJobId = s.colText(20);
    a.savedPrivateServerCode = s.colText(21);
    return a;
}

// ------------------------------------------------------------------ groups

QList<model::Group> AccountStore::groups() const
{
    QList<model::Group> out;
    Stmt s(m_db.handle(), QStringLiteral("SELECT id, name, color, collapsed, sort_order FROM groups ORDER BY sort_order, name"));
    while (s.step()) {
        model::Group g;
        g.id = s.colInt(0);
        g.name = s.colText(1);
        g.colorHex = s.colText(2);
        g.collapsed = s.colBool(3);
        g.sortOrder = static_cast<int>(s.colInt(4));
        out.append(g);
    }
    return out;
}

bool AccountStore::addGroup(model::Group& g)
{
    Stmt s(m_db.handle(), QStringLiteral(
        "INSERT INTO groups (name, color, collapsed, sort_order) VALUES (?,?,?,?)"));
    s.bind(1, g.name);
    s.bind(2, g.colorHex);
    s.bind(3, g.collapsed);
    s.bind(4, g.sortOrder);
    if (!s.exec())
        return false;
    g.id = s.lastInsertRowid(m_db.handle());
    return true;
}

bool AccountStore::updateGroup(const model::Group& g)
{
    Stmt s(m_db.handle(), QStringLiteral(
        "UPDATE groups SET name=?, color=?, collapsed=?, sort_order=? WHERE id=?"));
    s.bind(1, g.name);
    s.bind(2, g.colorHex);
    s.bind(3, g.collapsed);
    s.bind(4, g.sortOrder);
    s.bind(5, g.id);
    return s.exec();
}

bool AccountStore::deleteGroup(qint64 id)
{
    // The FK ON DELETE SET NULL exists in the schema, but we do not rely on the pragma:
    // accounts must end up without a group.
    {
        Stmt s(m_db.handle(), QStringLiteral("UPDATE accounts SET group_id=NULL WHERE group_id=?"));
        s.bind(1, id);
        s.exec();
    }
    Stmt s(m_db.handle(), QStringLiteral("DELETE FROM groups WHERE id=?"));
    s.bind(1, id);
    return s.exec();
}

// ------------------------------------------------------------------ accounts

QList<model::Account> AccountStore::accounts() const
{
    QList<model::Account> out;
    Stmt s(m_db.handle(),
           QStringLiteral("SELECT %1 FROM accounts ORDER BY sort_order, username").arg(kAccountCols));
    while (s.step())
        out.append(accountFromRow(s));
    return out;
}

std::optional<model::Account> AccountStore::accountById(qint64 id) const
{
    Stmt s(m_db.handle(),
           QStringLiteral("SELECT %1 FROM accounts WHERE id=?").arg(kAccountCols));
    s.bind(1, id);
    if (s.step())
        return accountFromRow(s);
    return std::nullopt;
}

std::optional<model::Account> AccountStore::accountByUserId(qint64 userId) const
{
    if (userId <= 0)
        return std::nullopt;
    Stmt s(m_db.handle(),
           QStringLiteral("SELECT %1 FROM accounts WHERE user_id=? ORDER BY id LIMIT 1").arg(kAccountCols));
    s.bind(1, userId);
    if (s.step())
        return accountFromRow(s);
    return std::nullopt;
}

std::optional<model::Account> AccountStore::accountByUsername(const QString& username) const
{
    Stmt s(m_db.handle(),
           QStringLiteral("SELECT %1 FROM accounts WHERE username=?").arg(kAccountCols));
    s.bind(1, username);
    if (s.step())
        return accountFromRow(s);
    return std::nullopt;
}

bool AccountStore::addAccount(model::Account& a)
{
    Stmt s(m_db.handle(), QStringLiteral(
        "INSERT INTO accounts (user_id, username, display_name, alias, description, group_id, avatar_url, "
        "favorite, sort_order, priority, tags, fields, has_session, cookie_expired, verified, moderation, "
        "last_used, last_validated, saved_place_id, saved_job_id, saved_private_server_code) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    bindAccount(s, a, false);
    if (!s.exec()) {
        EXLOG_WARN("store") << "addAccount failed for" << a.username << ":" << m_db.lastError();
        return false;
    }
    a.id = s.lastInsertRowid(m_db.handle());
    return true;
}

bool AccountStore::updateAccount(const model::Account& a)
{
    Stmt s(m_db.handle(), QStringLiteral(
        "UPDATE accounts SET user_id=?, username=?, display_name=?, alias=?, description=?, group_id=?, "
        "avatar_url=?, favorite=?, sort_order=?, priority=?, tags=?, fields=?, has_session=?, "
        "cookie_expired=?, verified=?, moderation=?, last_used=?, last_validated=?, "
        "saved_place_id=?, saved_job_id=?, saved_private_server_code=? WHERE id=?"));
    bindAccount(s, a, false);
    s.bind(22, a.id);
    if (!s.exec()) {
        EXLOG_WARN("store") << "updateAccount failed for" << a.username << ":" << m_db.lastError();
        return false;
    }
    return true;
}

bool AccountStore::deleteAccount(qint64 id)
{
    {
        Stmt s(m_db.handle(), QStringLiteral("DELETE FROM secret_entries WHERE account_id=?"));
        s.bind(1, id);
        s.exec();
    }
    Stmt s(m_db.handle(), QStringLiteral("DELETE FROM accounts WHERE id=?"));
    s.bind(1, id);
    return s.exec();
}

bool AccountStore::deleteAccounts(const QList<qint64>& ids)
{
    bool ok = true;
    for (qint64 id : ids)
        ok = deleteAccount(id) && ok;
    return ok;
}

bool AccountStore::reorderAccounts(const QList<qint64>& orderedIds)
{
    int order = 0;
    for (qint64 id : orderedIds) {
        Stmt s(m_db.handle(), QStringLiteral("UPDATE accounts SET sort_order=? WHERE id=?"));
        s.bind(1, order);
        s.bind(2, id);
        s.exec();
        ++order;
    }
    return true;
}

bool AccountStore::touchAccountUsed(qint64 id)
{
    Stmt s(m_db.handle(), QStringLiteral(
        "UPDATE accounts SET last_used=? WHERE id=?"));
    s.bind(1, QDateTime::currentDateTime().toString(Qt::ISODate));
    s.bind(2, id);
    return s.exec();
}

// ------------------------------------------------------------------ presets

QList<model::LaunchPreset> AccountStore::presets() const
{
    QList<model::LaunchPreset> out;
    Stmt s(m_db.handle(), QStringLiteral(
        "SELECT id, name, target, fps_cap, group_id, note, window FROM presets ORDER BY name"));
    while (s.step()) {
        model::LaunchPreset p;
        p.id = s.colInt(0);
        p.name = s.colText(1);
        const QJsonObject t = QJsonDocument::fromJson(s.colBlob(2)).object();
        p.target.kind = static_cast<model::LaunchTarget::Kind>(t[QStringLiteral("kind")].toInt(0));
        p.target.placeId = static_cast<qint64>(t[QStringLiteral("placeId")].toDouble(0));
        p.target.jobId = t[QStringLiteral("jobId")].toString();
        p.target.privateServerCode = t[QStringLiteral("privateServerCode")].toString();
        p.target.followUserId = static_cast<qint64>(t[QStringLiteral("followUserId")].toDouble(0));
        p.target.shareCode = t[QStringLiteral("shareCode")].toString();
        p.target.teleport = t[QStringLiteral("teleport")].toBool();
        p.fpsCap = static_cast<int>(s.colInt(3));
        p.groupId = s.colInt(4);
        p.note = s.colText(5);
        if (!s.colIsNull(6))
            p.window = model::WindowProfile::fromJson(QJsonDocument::fromJson(s.colBlob(6)).object());
        out.append(p);
    }
    return out;
}

bool AccountStore::addPreset(model::LaunchPreset& p)
{
    QJsonObject t;
    t[QStringLiteral("kind")] = static_cast<int>(p.target.kind);
    t[QStringLiteral("placeId")] = static_cast<double>(p.target.placeId);
    t[QStringLiteral("jobId")] = p.target.jobId;
    t[QStringLiteral("privateServerCode")] = p.target.privateServerCode;
    t[QStringLiteral("followUserId")] = static_cast<double>(p.target.followUserId);
    t[QStringLiteral("shareCode")] = p.target.shareCode;
    t[QStringLiteral("teleport")] = p.target.teleport;

    Stmt s(m_db.handle(), QStringLiteral(
        "INSERT INTO presets (name, target, fps_cap, group_id, note, window) VALUES (?,?,?,?,?,?)"));
    s.bind(1, p.name);
    s.bind(2, QJsonDocument(t).toJson(QJsonDocument::Compact));
    s.bind(3, p.fpsCap);
    s.bind(4, p.groupId);
    s.bind(5, p.note);
    p.window.enabled ? s.bind(6, QJsonDocument(p.window.toJson()).toJson(QJsonDocument::Compact))
                     : s.bindNull(6);
    if (!s.exec())
        return false;
    p.id = s.lastInsertRowid(m_db.handle());
    return true;
}

bool AccountStore::updatePreset(const model::LaunchPreset& p)
{
    QJsonObject t;
    t[QStringLiteral("kind")] = static_cast<int>(p.target.kind);
    t[QStringLiteral("placeId")] = static_cast<double>(p.target.placeId);
    t[QStringLiteral("jobId")] = p.target.jobId;
    t[QStringLiteral("privateServerCode")] = p.target.privateServerCode;
    t[QStringLiteral("followUserId")] = static_cast<double>(p.target.followUserId);
    t[QStringLiteral("shareCode")] = p.target.shareCode;
    t[QStringLiteral("teleport")] = p.target.teleport;

    Stmt s(m_db.handle(), QStringLiteral(
        "UPDATE presets SET name=?, target=?, fps_cap=?, group_id=?, note=?, window=? WHERE id=?"));
    s.bind(1, p.name);
    s.bind(2, QJsonDocument(t).toJson(QJsonDocument::Compact));
    s.bind(3, p.fpsCap);
    s.bind(4, p.groupId);
    s.bind(5, p.note);
    p.window.enabled ? s.bind(6, QJsonDocument(p.window.toJson()).toJson(QJsonDocument::Compact))
                     : s.bindNull(6);
    s.bind(7, p.id);
    return s.exec();
}

bool AccountStore::deletePreset(qint64 id)
{
    Stmt s(m_db.handle(), QStringLiteral("DELETE FROM presets WHERE id=?"));
    s.bind(1, id);
    return s.exec();
}

// ------------------------------------------------------------------ private servers

QList<model::PrivateServer> AccountStore::privateServers() const
{
    QList<model::PrivateServer> out;
    Stmt s(m_db.handle(), QStringLiteral(
        "SELECT id, owner_id, name, place_id, code, added, game_name, game_icon_url FROM private_servers ORDER BY name"));
    while (s.step()) {
        model::PrivateServer p;
        p.id = s.colInt(0);
        p.ownerId = s.colInt(1);
        p.name = s.colText(2);
        p.placeId = s.colInt(3);
        p.code = s.colText(4);
        if (!s.colIsNull(5))
            p.added = QDateTime::fromString(s.colText(5), Qt::ISODate);
        p.gameName = s.colText(6);
        p.gameIconUrl = s.colText(7);
        out.append(p);
    }
    return out;
}

bool AccountStore::addPrivateServer(model::PrivateServer& p)
{
    Stmt s(m_db.handle(), QStringLiteral(
        "INSERT INTO private_servers (owner_id, name, place_id, code, added, game_name, game_icon_url) "
        "VALUES (?,?,?,?,?,?,?)"));
    s.bind(1, p.ownerId);
    s.bind(2, p.name);
    s.bind(3, p.placeId);
    s.bind(4, p.code);
    s.bind(5, optionalIso(p.added.isValid() ? p.added : QDateTime::currentDateTime()));
    s.bind(6, p.gameName);
    s.bind(7, p.gameIconUrl);
    if (!s.exec())
        return false;
    p.id = s.lastInsertRowid(m_db.handle());
    return true;
}

bool AccountStore::updatePrivateServer(const model::PrivateServer& p)
{
    Stmt s(m_db.handle(), QStringLiteral(
        "UPDATE private_servers SET owner_id=?, name=?, place_id=?, code=?, game_name=?, game_icon_url=? WHERE id=?"));
    s.bind(1, p.ownerId);
    s.bind(2, p.name);
    s.bind(3, p.placeId);
    s.bind(4, p.code);
    s.bind(5, p.gameName);
    s.bind(6, p.gameIconUrl);
    s.bind(7, p.id);
    return s.exec();
}

bool AccountStore::deletePrivateServer(qint64 id)
{
    Stmt s(m_db.handle(), QStringLiteral("DELETE FROM private_servers WHERE id=?"));
    s.bind(1, id);
    return s.exec();
}

// ------------------------------------------------------------------ activity / ledger

bool AccountStore::logActivity(const QString& kind, qint64 accountId, const QString& detail)
{
    Stmt s(m_db.handle(), QStringLiteral(
        "INSERT INTO activity (kind, account_id, detail) VALUES (?,?,?)"));
    s.bind(1, kind);
    accountId ? s.bind(2, accountId) : s.bindNull(2);
    s.bind(3, detail);
    return s.exec();
}

bool AccountStore::sessionStart(qint64 accountId, qint64 placeId, const QString& jobId, qint64 pid, qint64& outLedgerId)
{
    Stmt s(m_db.handle(), QStringLiteral(
        "INSERT INTO session_ledger (account_id, place_id, job_id, started_at, pid) VALUES (?,?,?,?,?)"));
    s.bind(1, accountId);
    s.bind(2, placeId);
    s.bind(3, jobId);
    s.bind(4, QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    s.bind(5, pid);
    if (!s.exec())
        return false;
    outLedgerId = s.lastInsertRowid(m_db.handle());
    logActivity(QStringLiteral("session_start"), accountId, QString::number(placeId));
    return true;
}

bool AccountStore::sessionEnd(qint64 ledgerId, const QString& reason)
{
    Stmt s(m_db.handle(), QStringLiteral(
        "UPDATE session_ledger SET ended_at=?, ended_reason=? WHERE id=? AND ended_at IS NULL"));
    s.bind(1, QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    s.bind(2, reason);
    s.bind(3, ledgerId);
    return s.exec();
}

bool AccountStore::closeOpenSessions(const QString& reason)
{
    // ended_at = started_at: zero duration, app idle time is not counted.
    Stmt s(m_db.handle(), QStringLiteral(
        "UPDATE session_ledger SET ended_at=started_at, ended_reason=? WHERE ended_at IS NULL"));
    s.bind(1, reason);
    return s.exec();
}

bool AccountStore::markSessionValid(qint64 accountId)
{
    Stmt s(m_db.handle(), QStringLiteral(
        "UPDATE accounts SET has_session=1, cookie_expired=0, last_validated=? WHERE id=?"));
    s.bind(1, QDateTime::currentDateTime().toString(Qt::ISODate));
    s.bind(2, accountId);
    return s.exec();
}

namespace {
QString utcIso(const QDateTime& dt)
{
    return dt.toUTC().toString(Qt::ISODate);
}

QDateTime effectiveFrom(const QDateTime& from)
{
    return from.isValid() ? from : QDateTime::fromSecsSinceEpoch(0, QTimeZone::UTC);
}

QDateTime effectiveTo(const QDateTime& to)
{
    return to.isValid() ? to : QDateTime(QDate(2100, 1, 1), QTime(0, 0), QTimeZone::UTC);
}
} // namespace

qint64 AccountStore::sessionSecondsFor(qint64 accountId, const QDateTime& from,
                                       const QDateTime& to) const
{
    // Overlap of [started_at, ended_at|now] with [from,to), in seconds.
    // julianday() compares numerically — string T/Z formats do not interfere.
    const QString sql = QStringLiteral(
        "SELECT CAST(COALESCE(SUM("
        "  MAX(0.0, MIN(julianday(COALESCE(ended_at,'now')), julianday(?2))"
        "           - MAX(julianday(started_at), julianday(?1)))), 0.0) * 86400.0 AS INTEGER) "
        "FROM session_ledger "
        "WHERE account_id=?3 "
        "  AND julianday(COALESCE(ended_at,'now')) >= julianday(?1) "
        "  AND julianday(started_at) <= julianday(?2)");
    Stmt s(m_db.handle(), sql);
    s.bind(1, utcIso(effectiveFrom(from)));
    s.bind(2, utcIso(effectiveTo(to)));
    s.bind(3, accountId);
    if (!s.step())
        return 0;
    return s.colInt(0);
}

qint64 AccountStore::sessionSecondsBetween(const QDateTime& from, const QDateTime& to) const
{
    const QString sql = QStringLiteral(
        "SELECT CAST(COALESCE(SUM("
        "  MAX(0.0, MIN(julianday(COALESCE(ended_at,'now')), julianday(?2))"
        "           - MAX(julianday(started_at), julianday(?1)))), 0.0) * 86400.0 AS INTEGER) "
        "FROM session_ledger "
        "WHERE julianday(COALESCE(ended_at,'now')) >= julianday(?1) "
        "  AND julianday(started_at) <= julianday(?2)");
    Stmt s(m_db.handle(), sql);
    s.bind(1, utcIso(effectiveFrom(from)));
    s.bind(2, utcIso(effectiveTo(to)));
    if (!s.step())
        return 0;
    return s.colInt(0);
}

qint64 AccountStore::sessionSecondsToday() const
{
    return sessionSecondsBetween(QDateTime(QDate::currentDate(), QTime(0, 0)), {});
}

QList<QDateTime> AccountStore::sessionStarts(const QDateTime& from,
                                             const QDateTime& to) const
{
    QList<QDateTime> out;
    Stmt s(m_db.handle(), QStringLiteral(
        "SELECT started_at FROM session_ledger "
        "WHERE julianday(started_at) >= julianday(?1) "
        "  AND julianday(started_at) < julianday(?2) "
        "ORDER BY started_at"));
    s.bind(1, utcIso(effectiveFrom(from)));
    s.bind(2, utcIso(effectiveTo(to)));
    while (s.step()) {
        QDateTime dt = QDateTime::fromString(s.colText(0), Qt::ISODate);
        if (dt.isValid())
            out.append(dt.toUTC());
    }
    return out;
}

int AccountStore::sessionCountFor(qint64 accountId, const QDateTime& from,
                                  const QDateTime& to) const
{
    const QString sql = QStringLiteral(
        "SELECT COUNT(*) FROM session_ledger "
        "WHERE account_id=?3 "
        "  AND julianday(COALESCE(ended_at,'now')) >= julianday(?1) "
        "  AND julianday(started_at) <= julianday(?2)");
    Stmt s(m_db.handle(), sql);
    s.bind(1, utcIso(effectiveFrom(from)));
    s.bind(2, utcIso(effectiveTo(to)));
    s.bind(3, accountId);
    if (!s.step())
        return 0;
    return static_cast<int>(s.colInt(0));
}

} // namespace ex::store

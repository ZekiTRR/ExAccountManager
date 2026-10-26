#pragma once

#include "core/models/account.h"
#include "core/models/domain.h"
#include "core/store/database.h"

namespace ex::store {

// CRUD аккаунтов, групп, пресетов, приватных серверов, activity и ledger.
class AccountStore {
public:
    explicit AccountStore(Database& db) : m_db(db) {}

    // --- группы ---
    QList<model::Group> groups() const;
    bool addGroup(model::Group& g);
    bool updateGroup(const model::Group& g);
    bool deleteGroup(qint64 id); // аккаунты остаются, group_id -> NULL

    // --- аккаунты ---
    QList<model::Account> accounts() const;
    std::optional<model::Account> accountById(qint64 id) const;
    std::optional<model::Account> accountByUserId(qint64 userId) const;
    std::optional<model::Account> accountByUsername(const QString& username) const;
    bool addAccount(model::Account& a);          // a.id заполняется
    bool updateAccount(const model::Account& a); // по id
    bool deleteAccount(qint64 id);               // + секреты (ON DELETE CASCADE)
    bool deleteAccounts(const QList<qint64>& ids);
    bool reorderAccounts(const QList<qint64>& orderedIds);
    bool touchAccountUsed(qint64 id);

    // --- пресеты ---
    QList<model::LaunchPreset> presets() const;
    bool addPreset(model::LaunchPreset& p);
    bool updatePreset(const model::LaunchPreset& p);
    bool deletePreset(qint64 id);

    // --- приватные серверы [feature #4] ---
    QList<model::PrivateServer> privateServers() const;
    bool addPrivateServer(model::PrivateServer& p);
    bool updatePrivateServer(const model::PrivateServer& p);
    bool deletePrivateServer(qint64 id);

    // --- activity / аналитика ---
    bool logActivity(const QString& kind, qint64 accountId, const QString& detail);
    bool sessionStart(qint64 accountId, qint64 placeId, const QString& jobId, qint64 pid, qint64& outLedgerId);
    bool sessionEnd(qint64 ledgerId, const QString& reason);
    // Закрыть "осиротевшие" сессии (прошлый запуск приложения/краш): ended=started,
    // чтобы простой между запусками не засчитывался как время в игре.
    bool closeOpenSessions(const QString& reason);
    // Успешный запуск: сессия аккаунта точно жива (снимаем ложный cookie_expired).
    bool markSessionValid(qint64 accountId);

    // Сколько аккаунт провёл в игре (сек) за период [from,to); пустые = без границы.
    qint64 sessionSecondsFor(qint64 accountId, const QDateTime& from, const QDateTime& to) const;
    // То же суммарно по всем аккаунтам.
    qint64 sessionSecondsBetween(const QDateTime& from, const QDateTime& to) const;
    // Суммарно по всем аккаунтам за сегодня (локальная полночь).
    qint64 sessionSecondsToday() const;
    // Число сессий (запусков) аккаунта за период.
    int sessionCountFor(qint64 accountId, const QDateTime& from, const QDateTime& to) const;
    // Времена старта сессий за период (для графика активности, UTC).
    QList<QDateTime> sessionStarts(const QDateTime& from, const QDateTime& to) const;

private:
    static model::Account accountFromRow(Stmt& s);
    Database& m_db;
};

} // namespace ex::store

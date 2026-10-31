#pragma once

#include "core/models/account.h"
#include "core/models/domain.h"
#include "core/store/database.h"

namespace ex::store {

// CRUD for accounts, groups, presets, private servers, activity and ledger.
class AccountStore {
public:
    explicit AccountStore(Database& db) : m_db(db) {}

    // --- groups ---
    QList<model::Group> groups() const;
    bool addGroup(model::Group& g);
    bool updateGroup(const model::Group& g);
    bool deleteGroup(qint64 id); // accounts stay, group_id -> NULL

    // --- accounts ---
    QList<model::Account> accounts() const;
    std::optional<model::Account> accountById(qint64 id) const;
    std::optional<model::Account> accountByUserId(qint64 userId) const;
    std::optional<model::Account> accountByUsername(const QString& username) const;
    bool addAccount(model::Account& a);          // a.id gets filled in
    bool updateAccount(const model::Account& a); // by id
    bool deleteAccount(qint64 id);               // + secrets (ON DELETE CASCADE)
    bool deleteAccounts(const QList<qint64>& ids);
    bool reorderAccounts(const QList<qint64>& orderedIds);
    bool touchAccountUsed(qint64 id);

    // --- presets ---
    QList<model::LaunchPreset> presets() const;
    bool addPreset(model::LaunchPreset& p);
    bool updatePreset(const model::LaunchPreset& p);
    bool deletePreset(qint64 id);

    // --- private servers [feature #4] ---
    QList<model::PrivateServer> privateServers() const;
    bool addPrivateServer(model::PrivateServer& p);
    bool updatePrivateServer(const model::PrivateServer& p);
    bool deletePrivateServer(qint64 id);

    // --- activity / analytics ---
    bool logActivity(const QString& kind, qint64 accountId, const QString& detail);
    bool sessionStart(qint64 accountId, qint64 placeId, const QString& jobId, qint64 pid, qint64& outLedgerId);
    bool sessionEnd(qint64 ledgerId, const QString& reason);
    // Close orphaned sessions (previous run or crash): ended=started,
    // so idle time between launches is not counted as time in game.
    bool closeOpenSessions(const QString& reason);
    // Successful launch: the account session is definitely alive (clears a false cookie_expired).
    bool markSessionValid(qint64 accountId);

    // Seconds played in [from,to); empty bounds mean no limit.
    qint64 sessionSecondsFor(qint64 accountId, const QDateTime& from, const QDateTime& to) const;
    // Same totals across all accounts.
    qint64 sessionSecondsBetween(const QDateTime& from, const QDateTime& to) const;
    // Total across all accounts today (from local midnight).
    qint64 sessionSecondsToday() const;
    // Number of sessions (launches) for an account in the period.
    int sessionCountFor(qint64 accountId, const QDateTime& from, const QDateTime& to) const;
    // Session start times in a period (activity chart, UTC).
    QList<QDateTime> sessionStarts(const QDateTime& from, const QDateTime& to) const;

private:
    static model::Account accountFromRow(Stmt& s);
    Database& m_db;
};

} // namespace ex::store

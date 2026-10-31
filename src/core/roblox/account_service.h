#pragma once

#include "core/models/account.h"
#include "core/roblox/api.h"
#include "core/roblox/bulk_import.h"
#include "core/store/account_store.h"
#include "core/store/vault.h"

#include <QHash>
#include <QObject>

namespace ex::roblox {

// Account management service: add by cookie (validate BEFORE saving, the
// Astro principle), bulk import, revalidation, presence and avatars.
class AccountService : public QObject {
    Q_OBJECT
public:
    AccountService(RobloxApi& api, ex::store::AccountStore& store, ex::store::Vault& vault,
                   QObject* parent = nullptr);

    // Add an account by cookie: validate -> save the account -> cookie into the vault.
    void addFromCookie(const QString& cookie,
                       std::function<void(bool ok, qint64 accountId, QString error)> cb);

    // Add with account data that is ALREADY known (from fetch('/my/account/json')
    // inside the browser page, the C# RAM AddAccount AccountJSON pattern).
    // username is the Roblox name, displayName is the display name (they may differ).
    void addAccountValidated(const QString& cookie, qint64 userId,
                             const QString& username, const QString& displayName,
                             const QString& password,
                             std::function<void(bool ok, qint64 accountId, QString error)> cb);

    // Bulk import: validates one entry at a time (to avoid rate limits, the RM
    // approach) and saves each entry.
    void importBulk(const QList<ex::roblox::BulkEntry>& entries,
                    std::function<void(int added, int failed)> cb);

    // Revalidate every account: refresh the cookie_expired flag and moderation info.
    void revalidateAll(std::function<void()> cb);

    // Refresh presence for the given accounts; an empty list means all accounts.
    // Uses the first live cookie.
    void refreshPresence(const QList<qint64>& accountIds);
    model::Presence presenceFor(qint64 accountId) const;

    // Refresh avatars (unauthenticated, in batches). An empty list means all accounts.
    void refreshAvatars(const QList<qint64>& userIds);

    // Persist a new .ROBLOSECURITY issued by the server via Set-Cookie.
    void applyRotatedCookie(const QString& oldCookie, const QString& newCookie);
    // Mark the account session as invalid (e.g. a 401 during launch).
    void markSessionInvalid(qint64 accountId);

    bool vaultUnlocked() const { return m_vault.isUnlocked(); }

signals:
    void accountAdded(qint64 accountId);
    void accountUpdated(qint64 accountId);
    void presenceUpdated();
    void avatarsUpdated();
    void importProgress(int done, int total);

private:
    void importNext(const QList<ex::roblox::BulkEntry>& entries, int index,
                    int added, int failed,
                    std::function<void(int, int)> cb);
    bool saveCookie(qint64 accountId, const QString& cookie);

    RobloxApi& m_api;
    ex::store::AccountStore& m_store;
    ex::store::Vault& m_vault;
    QHash<qint64, model::Presence> m_presence; // session cache, never written to the DB
};

} // namespace ex::roblox

#include "core/roblox/account_service.h"

#include "core/roblox/bulk_import.h"
#include "core/util/log.h"
#include "core/util/redact.h"

#include <QTimer>

#include <memory>

namespace ex::roblox {

AccountService::AccountService(RobloxApi& api, ex::store::AccountStore& store,
                               ex::store::Vault& vault, QObject* parent)
    : QObject(parent)
    , m_api(api)
    , m_store(store)
    , m_vault(vault)
{
}

bool AccountService::saveCookie(qint64 accountId, const QString& cookie)
{
    if (!m_vault.isUnlocked())
        return false;
    return m_vault.setCookie(accountId, cookie);
}

void AccountService::addFromCookie(const QString& rawCookie,
                                   std::function<void(bool, qint64, QString)> cb)
{
    // Validate BEFORE saving: identify the account through the API and only
    // then put the cookie into the vault (the Astro principle).
    const QString cookie = ex::util::redact::normalizeCookieValue(rawCookie);
    if (!ex::util::redact::looks_like_roblox_cookie(cookie)) {
        cb(false, 0, QStringLiteral("not-a-cookie"));
        return;
    }
    if (!m_vault.isUnlocked()) {
        cb(false, 0, QStringLiteral("vault-locked"));
        return;
    }

    EXLOG_INFO("accounts") << "validating cookie, len" << cookie.size();
    m_api.validateCookie(cookie, [this, cookie, cb = std::move(cb)](const CookieValidation& v) {
        if (v.check != SessionCheck::Ok) {
            EXLOG_WARN("accounts") << "cookie validation failed:" << v.error;
            cb(false, 0, v.error);
            return;
        }
        const CookieValidation validation = v;
        const QString validatedCookie =
            v.effectiveCookie.isEmpty() ? cookie : v.effectiveCookie;

        auto saveAccount = [this, validation, cb](const QString& finalCookie) {
            // Deduplicate the same way RAM does: by user ID first (usernames can
            // change), then by username (for legacy rows without a user ID).
            auto existing = m_store.accountByUserId(validation.user.userId);
            if (!existing)
                existing = m_store.accountByUsername(validation.user.username);
            if (existing) {
                if (!saveCookie(existing->id, finalCookie)) {
                    cb(false, 0, QStringLiteral("vault-locked"));
                    return;
                }
                existing->userId = validation.user.userId;
                existing->username = validation.user.username;
                existing->displayName = validation.user.displayName;
                existing->hasSession = true;
                existing->cookieExpired = false;
                existing->lastValidated = QDateTime::currentDateTime();
                if (!m_store.updateAccount(*existing)) {
                    cb(false, existing->id, QStringLiteral("store-failed"));
                    return;
                }
                emit accountUpdated(existing->id);
                cb(true, existing->id, QString());
                return;
            }

            ex::model::Account account;
            account.userId = validation.user.userId;
            account.username = validation.user.username;
            account.displayName = validation.user.displayName;
            account.hasSession = true;
            account.lastValidated = QDateTime::currentDateTime();
            if (!m_store.addAccount(account)) {
                cb(false, 0, QStringLiteral("store-failed"));
                return;
            }
            if (!saveCookie(account.id, finalCookie)) {
                m_store.deleteAccount(account.id);
                cb(false, 0, QStringLiteral("vault-locked"));
                return;
            }
            m_store.logActivity(QStringLiteral("account_added"), account.id,
                                validation.user.username);
            EXLOG_INFO("accounts") << "account added:" << validation.user.username;
            emit accountAdded(account.id);
            cb(true, account.id, QString());
        };

        // Web validation is the only mandatory step (as in RAM/RM/Astro).
        // The auth ticket is checked at launch: a false preflight rejection must
        // not block adding a cookie that is known to work.
        saveAccount(validatedCookie);
    });
}

void AccountService::addAccountValidated(const QString& rawCookie, qint64 userId,
                                         const QString& username, const QString& displayName,
                                         const QString& password,
                                         std::function<void(bool, qint64, QString)> cb)
{
    // The data already came from fetch('/my/account/json') on an authenticated
    // browser page, so no second HTTP validation is needed (the C# AddAccount pattern).
    const QString cookie = ex::util::redact::normalizeCookieValue(rawCookie);
    const QString name = username.trimmed().isEmpty() ? displayName.trimmed() : username.trimmed();
    const QString display = displayName.trimmed().isEmpty() ? name : displayName.trimmed();
    if (cookie.isEmpty() || userId <= 0 || name.isEmpty()) {
        cb(false, 0, QStringLiteral("bad-account-info"));
        return;
    }
    if (!m_vault.isUnlocked()) {
        cb(false, 0, QStringLiteral("vault-locked"));
        return;
    }

    auto saveAccount = [this, userId, name, display, password, cb](
                           const QString& finalCookie) {
        auto existing = m_store.accountByUserId(userId);
        if (!existing)
            existing = m_store.accountByUsername(name);
        if (existing) {
            if (!saveCookie(existing->id, finalCookie)) {
                cb(false, 0, QStringLiteral("vault-locked"));
                return;
            }
            existing->hasSession = true;
            existing->cookieExpired = false;
            existing->lastValidated = QDateTime::currentDateTime();
            existing->userId = userId;
            existing->username = name;
            existing->displayName = display;
            if (!m_store.updateAccount(*existing)) {
                cb(false, existing->id, QStringLiteral("store-failed"));
                return;
            }
            if (!password.isEmpty())
                m_vault.setPassword(existing->id, password);
            emit accountUpdated(existing->id);
            EXLOG_INFO("accounts") << "account updated:" << name;
            cb(true, existing->id, QString());
            return;
        }

        ex::model::Account account;
        account.userId = userId;
        account.username = name;
        account.displayName = display;
        account.hasSession = true;
        account.lastValidated = QDateTime::currentDateTime();
        if (!m_store.addAccount(account)) {
            cb(false, 0, QStringLiteral("store-failed"));
            return;
        }
        if (!saveCookie(account.id, finalCookie)) {
            m_store.deleteAccount(account.id);
            cb(false, 0, QStringLiteral("vault-locked"));
            return;
        }
        if (!password.isEmpty())
            m_vault.setPassword(account.id, password);
        m_store.logActivity(QStringLiteral("account_added"), account.id, name);
        EXLOG_INFO("accounts") << "account added:" << name;
        emit accountAdded(account.id);
        cb(true, account.id, QString());
    };

    // The data has already been confirmed by the authenticated browser page;
    // the ticket is only checked at launch (see the comment in addFromCookie).
    saveAccount(cookie);
}

void AccountService::importBulk(const QList<ex::roblox::BulkEntry>& entries,
                                std::function<void(int, int)> cb)
{
    if (entries.isEmpty()) {
        cb(0, 0);
        return;
    }
    importNext(entries, 0, 0, 0, std::move(cb));
}

void AccountService::importNext(const QList<ex::roblox::BulkEntry>& entries, int index,
                                int added, int failed,
                                std::function<void(int, int)> cb)
{
    if (index >= entries.size()) {
        EXLOG_INFO("accounts") << "bulk import finished: added" << added << "failed" << failed;
        cb(added, failed);
        return;
    }
    const auto& e = entries.at(index);
    emit importProgress(index + 1, entries.size());

    auto next = [this, entries, index, added, failed, cb](bool ok, qint64, const QString&) {
        // Short pause between requests to stay under rate limits (the RM approach)
        QTimer::singleShot(700, this, [this, entries, index, added, failed, ok, cb]() {
            importNext(entries, index + 1, added + (ok ? 1 : 0), failed + (ok ? 0 : 1), cb);
        });
    };

    if (!e.cookie.isEmpty()) {
        addFromCookie(e.cookie, next);
    } else if (!e.username.isEmpty() && !e.password.isEmpty()) {
        // Username/password login is handled by the CDP dialog in the UI.
        next(false, 0, QStringLiteral("login-by-password-via-ui"));
    } else {
        next(false, 0, QStringLiteral("empty-entry"));
    }
}

void AccountService::revalidateAll(std::function<void()> cb)
{
    if (!m_vault.isUnlocked()) {
        EXLOG_WARN("accounts") << "revalidate skipped: vault is locked";
        cb();
        return;
    }
    const auto accounts = m_store.accounts();
    if (accounts.isEmpty()) {
        cb();
        return;
    }

    struct Agg {
        int left = 0;
        std::function<void()> done;
    };
    auto agg = std::make_shared<Agg>();
    agg->left = static_cast<int>(accounts.size());
    agg->done = std::move(cb);
    auto finishOne = [agg]() {
        if (--agg->left == 0 && agg->done)
            agg->done();
    };

    for (const auto& acc : accounts) {
        if (!acc.hasSession) {
            finishOne();
            continue;
        }
        const QString cookie = m_vault.cookie(acc.id);
        if (cookie.isEmpty()) {
            auto a = acc;
            a.cookieExpired = true;
            a.hasSession = false;
            m_store.updateAccount(a);
            emit accountUpdated(a.id);
            finishOne();
            continue;
        }
        m_api.validateCookie(cookie,
                             [this, acc, cookie, finishOne](const CookieValidation& v) mutable {
            auto a = acc;
            if (v.check == SessionCheck::InvalidSession) {
                // The Qt users endpoint may return a false 401 for a session that
                // really starts the game (the auth ticket goes through curl). We do not
                // mark cookie_expired from it: the only reliable signal is
                // an auth-ticket failure during launch.
                EXLOG_WARN("accounts") << "validation 401, keeping session state";
                finishOne();
                return;
            }
            if (v.check != SessionCheck::Ok) {
                // Network/server errors leave the status alone; the session may be alive.
                finishOne();
                return;
            }

            const QString effectiveCookie =
                v.effectiveCookie.isEmpty() ? cookie : v.effectiveCookie;
            if (effectiveCookie != cookie)
                saveCookie(a.id, effectiveCookie);

            a.userId = v.user.userId;
            a.username = v.user.username;
            a.displayName = v.user.displayName;
            a.cookieExpired = false;
            a.hasSession = true;
            a.lastValidated = QDateTime::currentDateTime();

            m_api.moderationStatus(effectiveCookie, a.userId,
                                   [this, a, finishOne](bool, model::ModerationInfo mod) mutable {
                a.moderation = mod;
                m_store.updateAccount(a);
                emit accountUpdated(a.id);
                finishOne();
            });
        });
    }
}

model::Presence AccountService::presenceFor(qint64 accountId) const
{
    return m_presence.value(accountId);
}

void AccountService::refreshPresence(const QList<qint64>& accountIds)
{
    if (!m_vault.isUnlocked()) {
        emit presenceUpdated();
        return;
    }
    const auto accounts = m_store.accounts();
    QString anyCookie;
    QList<qint64> userIds;
    for (const auto& a : accounts) {
        if (!accountIds.isEmpty() && !accountIds.contains(a.id))
            continue;
        if (a.userId > 0)
            userIds.append(a.userId);
        if (anyCookie.isEmpty() && a.hasSession && !a.cookieExpired)
            anyCookie = m_vault.cookie(a.id);
    }
    if (userIds.isEmpty() || anyCookie.isEmpty()) {
        emit presenceUpdated();
        return;
    }

    m_api.presence(anyCookie, userIds,
                   [this](bool ok, QList<model::Presence> presences) {
        if (!ok)
            return;
        for (const auto& p : presences) {
            if (p.userId > 0)
                m_presence.insert(p.userId, p);
        }
        emit presenceUpdated();
    });
}

void AccountService::refreshAvatars(const QList<qint64>& userIds)
{
    QList<qint64> ids = userIds;
    if (ids.isEmpty()) {
        for (const auto& a : m_store.accounts()) {
            if (a.userId > 0)
                ids.append(a.userId);
        }
    }
    if (ids.isEmpty())
        return;

    m_api.avatarHeadshots(ids, [this](bool ok, const QMap<qint64, QString>& urls) {
        if (!ok)
            return;
        bool changed = false;
        for (auto a : m_store.accounts()) {
            const QString url = urls.value(a.userId);
            if (!url.isEmpty() && url != a.avatarUrl) {
                a.avatarUrl = url;
                m_store.updateAccount(a);
                changed = true;
            }
        }
        if (changed)
            emit avatarsUpdated();
    });
}

void AccountService::applyRotatedCookie(const QString& oldCookie, const QString& newCookie)
{
    if (oldCookie.isEmpty() || newCookie.isEmpty() || oldCookie == newCookie)
        return;
    bool changed = false;
    for (auto a : m_store.accounts()) {
        if (!a.hasSession)
            continue;
        if (m_vault.cookie(a.id) != oldCookie)
            continue;
        if (!saveCookie(a.id, newCookie))
            continue;
        a.cookieExpired = false;
        a.lastValidated = QDateTime::currentDateTime();
        m_store.updateAccount(a);
        emit accountUpdated(a.id);
        changed = true;
        EXLOG_INFO("accounts") << "stored rotated cookie for account" << a.id;
    }
    if (changed)
        m_store.logActivity(QStringLiteral("cookie_rotated"), 0, QString());
}

void AccountService::markSessionInvalid(qint64 accountId)
{
    auto acc = m_store.accountById(accountId);
    if (!acc)
        return;
    acc->cookieExpired = true;
    m_store.updateAccount(*acc);
    emit accountUpdated(accountId);
}

} // namespace ex::roblox

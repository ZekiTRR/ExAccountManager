#pragma once

#include "core/models/account.h"
#include "core/roblox/api.h"
#include "core/roblox/bulk_import.h"
#include "core/store/account_store.h"
#include "core/store/vault.h"

#include <QHash>
#include <QObject>

namespace ex::roblox {

// Сервис управления аккаунтами: добавление по cookie (с валидацией ДО
// сохранения — принцип Astro), bulk-импорт, ревалидация, presence/аватары.
class AccountService : public QObject {
    Q_OBJECT
public:
    AccountService(RobloxApi& api, ex::store::AccountStore& store, ex::store::Vault& vault,
                   QObject* parent = nullptr);

    // Добавить аккаунт по cookie: валидация -> сохранение аккаунта -> cookie в vault.
    void addFromCookie(const QString& cookie,
                       std::function<void(bool ok, qint64 accountId, QString error)> cb);

    // Добавить с УЖЕ ГОТОВЫМИ данными аккаунта (из fetch('/my/account/json')
    // внутри страницы браузера — паттерн C# RAM AddAccount с AccountJSON).
    // username — Roblox Name, displayName — DisplayName (могут различаться).
    void addAccountValidated(const QString& cookie, qint64 userId,
                             const QString& username, const QString& displayName,
                             const QString& password,
                             std::function<void(bool ok, qint64 accountId, QString error)> cb);

    // Bulk: валидирует по одному (анти rate-limit, приём RM), сохраняет.
    void importBulk(const QList<ex::roblox::BulkEntry>& entries,
                    std::function<void(int added, int failed)> cb);

    // Ревалидация всех: cookie expired флаг + модерация.
    void revalidateAll(std::function<void()> cb);

    // Обновить presence для списка аккаунтов (использует первый живой cookie).
    void refreshPresence(const QList<qint64>& accountIds);
    model::Presence presenceFor(qint64 accountId) const;

    // Обновить аватары (без авторизации, батчами). Пустой список = все.
    void refreshAvatars(const QList<qint64>& userIds);

    // Сохранить новый .ROBLOSECURITY, выданный сервером через Set-Cookie.
    void applyRotatedCookie(const QString& oldCookie, const QString& newCookie);
    // Пометить сессию аккаунта недействительной (401 при запуске и т.п.).
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
    QHash<qint64, model::Presence> m_presence; // сессионный кэш, в БД не пишется
};

} // namespace ex::roblox

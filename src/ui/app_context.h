#pragma once

#include "core/crypto/envelope.h"
#include "core/instances/instance_monitor.h"
#include "core/instances/instance_registry.h"
#include "core/launcher/launch_service.h"
#include "core/roblox/account_service.h"
#include "core/roblox/api.h"
#include "core/store/account_store.h"
#include "core/store/database.h"
#include "core/store/settings.h"
#include "core/store/vault.h"

#include <QObject>

namespace ex::ui {

// App context: owns every service and wires them together.
class AppContext : public QObject {
    Q_OBJECT
public:
    explicit AppContext(QObject* parent = nullptr);
    ~AppContext() override;

    bool init();          // true = ready to work
    bool vaultUnlocked(); // whether the secret vault is unlocked
    bool vaultNeedsPassword() const { return m_vaultError == QLatin1String("password-required"); }
    void unlockWithPassword(const QString& password, bool& ok);
    // Recreate the vault (old cookies cannot be decrypted -> sessions reset).
    bool resetVault();

    ex::store::Database& db() { return m_db; }
    ex::store::AccountStore& store() { return *m_store; }
    ex::store::Vault& vault() { return m_vault; }
    ex::store::Settings& settings() { return *m_settings; }
    ex::roblox::RobloxApi& api() { return *m_api; }
    ex::roblox::AccountService& accounts() { return *m_accountService; }
    ex::launcher::LaunchService& launcher() { return *m_launchService; }
    ex::instances::InstanceRegistry& registry() { return *m_registry; }
    ex::instances::InstanceMonitor& monitor() { return *m_monitor; }

    QString vaultError() const { return m_vaultError; }

    // Selected accounts (bulk operations) — shared UI state.
    QList<qint64> selectedAccountIds() const { return m_selectedAccounts; }
    void setSelectedAccountIds(const QList<qint64>& ids) { m_selectedAccounts = ids; }

signals:
    void accountsChanged();
    void vaultLocked();

private:
    bool openOrCreateVault();

    ex::store::Database m_db;
    std::unique_ptr<ex::store::AccountStore> m_store;
    ex::store::Vault m_vault;
    std::unique_ptr<ex::store::Settings> m_settings;
    std::unique_ptr<ex::roblox::RobloxApi> m_api;
    std::unique_ptr<ex::roblox::AccountService> m_accountService;
    ex::instances::InstanceRegistry* m_registry = nullptr; // child
    ex::instances::InstanceMonitor* m_monitor = nullptr;   // child
    std::unique_ptr<ex::launcher::LaunchService> m_launchService;

    QByteArray m_dataKey;
    QString m_vaultError;
    QList<qint64> m_selectedAccounts;
};

} // namespace ex::ui

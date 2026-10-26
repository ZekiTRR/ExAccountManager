#include "ui/app_context.h"

#include "core/launcher/client_settings.h"
#include "core/util/log.h"
#include "core/util/paths.h"

#include <QDir>

namespace ex::ui {

static const QString kVaultFile = QStringLiteral("/vault.dat");
static const QString kCredService = QStringLiteral("ExAccountManager");

AppContext::AppContext(QObject* parent)
    : QObject(parent)
    , m_vault(m_db)
{
}

AppContext::~AppContext() = default;

bool AppContext::init()
{
    ex::util::AppPaths::init();
    ex::log::init(ex::util::AppPaths::logsDir(), 7);
    EXLOG_INFO("app") << "ExAccountManager starting, portable:" << ex::util::AppPaths::portable();

    if (!m_db.open(ex::util::AppPaths::dbPath())) {
        m_vaultError = QStringLiteral("db-open-failed");
        EXLOG_ERROR("app") << "cannot open database";
        return false;
    }
    m_store = std::make_unique<ex::store::AccountStore>(m_db);
    m_settings = std::make_unique<ex::store::Settings>(m_db);

    // Сессии прошлого запуска не должны считаться фармом до бесконечности.
    if (!m_store->closeOpenSessions(QStringLiteral("orphaned")))
        EXLOG_WARN("app") << "cannot close orphaned sessions";

    // FPS-лимит больше не поддерживается: снимаем ключ, выставленный
    // прошлыми версиями/пользователем (клиент будет без капа).
    if (!ex::launcher::ClientSettings::clearFpsCap())
        EXLOG_WARN("app") << "cannot clear legacy FPS cap";

    // Дефолты настроек
    QVariantHash defaults;
    defaults.insert(QStringLiteral("general.language"), QStringLiteral("ru"));
    defaults.insert(QStringLiteral("general.theme"), QStringLiteral("dark"));
    defaults.insert(QStringLiteral("general.startMinimized"), false);
    defaults.insert(QStringLiteral("launcher.concurrency"), 3);
    defaults.insert(QStringLiteral("launcher.delaySeconds"), 4);
    defaults.insert(QStringLiteral("monitor.intervalMs"), 6000);
    defaults.insert(QStringLiteral("presence.enabled"), true);
    defaults.insert(QStringLiteral("presence.intervalMs"), 60000);
    defaults.insert(QStringLiteral("multiinstance.enabled"), true);
    m_settings->registerDefaults(defaults);

    if (!openOrCreateVault()) {
        // Хранилище не разблокировано: приложение работает в read-only по секретам.
        EXLOG_WARN("app") << "vault not unlocked:" << m_vaultError;
    } else {
        m_vault.setDataKey(m_dataKey);
    }

    // Сервисы (создаются всегда: без vault просто не будет cookie)
    m_api = std::make_unique<ex::roblox::RobloxApi>(this);
    m_registry = new ex::instances::InstanceRegistry(this);
    m_monitor = new ex::instances::InstanceMonitor(*m_registry, this);
    m_monitor->setIntervalMs(m_settings->get(QStringLiteral("monitor.intervalMs")).toInt());
    m_accountService = std::make_unique<ex::roblox::AccountService>(*m_api, *m_store, m_vault, this);
    m_launchService = std::make_unique<ex::launcher::LaunchService>(
        *m_api, *m_store, m_vault, *m_registry, this);

    // Реакция на изменения аккаунтов
    connect(m_accountService.get(), &ex::roblox::AccountService::accountAdded,
            this, &AppContext::accountsChanged);
    connect(m_accountService.get(), &ex::roblox::AccountService::accountUpdated,
            this, &AppContext::accountsChanged);
    connect(m_api.get(), &ex::roblox::RobloxApi::cookieRotated, this,
            [this](const QString& oldCookie, const QString& newCookie) {
        // Roblox (с 2026) ротирует .ROBLOSECURITY через Set-Cookie: без
        // сохранения нового значения следующие запросы получат 401.
        m_accountService->applyRotatedCookie(oldCookie, newCookie);
    });
    connect(m_launchService.get(), &ex::launcher::LaunchService::sessionInvalid, this,
            [this](qint64 accountId, const QString& error) {
        EXLOG_WARN("app") << "session invalid for account" << accountId << error;
        m_accountService->markSessionInvalid(accountId);
    });
    connect(m_launchService.get(), &ex::launcher::LaunchService::launchFinished,
            this, [this](const ex::launcher::LaunchResult& r) {
        if (!r.ok)
            EXLOG_WARN("app") << "launch failed:" << r.error << "account" << r.accountId;
        emit accountsChanged();
    });

    return true;
}

bool AppContext::openOrCreateVault()
{
    const QString vaultPath = ex::util::AppPaths::dataDir() + kVaultFile;
    if (!QFile::exists(vaultPath)) {
        // Создаём device-mode vault (ключ в Credential Manager, пароль не нужен).
        if (!ex::crypto::VaultKey::create(vaultPath, QString(), kCredService)) {
            m_vaultError = QStringLiteral("vault-create-failed");
            return false;
        }
        EXLOG_INFO("vault") << "created device-mode vault";
    }
    QString err;
    if (ex::crypto::VaultKey::unlock(vaultPath, QString(), kCredService, m_dataKey, err)) {
        m_vaultError.clear();
        // Миграция legacy (Credential Manager) -> DPAPI-файл: перезапаковываем
        // заголовок тем же data-ключом, ключ теперь в <vault>.key. Cookie
        // в БД не трогаются (дешёвый rekey, приём RM).
        const auto header = ex::crypto::VaultKey::readHeader(vaultPath);
        if (header.mode == ex::crypto::VaultKey::Mode::Device
            && !QFile::exists(ex::crypto::VaultKey::deviceKeyFilePath(vaultPath))) {
            if (ex::crypto::VaultKey::rekey(vaultPath, m_dataKey, QString(), kCredService)) {
                ex::crypto::VaultKey::deleteLegacyCredentialKey(kCredService);
                EXLOG_INFO("vault") << "migrated device key to dpapi file";
            } else {
                EXLOG_WARN("vault") << "dpapi migration failed — staying on legacy key";
            }
        }
        return true;
    }
    if (err == QStringLiteral("password-required")) {
        m_vaultError = QStringLiteral("password-required");
        emit vaultLocked();
        return false;
    }
    if (err == QStringLiteral("device-key-missing")) {
        m_vaultError = QStringLiteral("device-key-missing");
        emit vaultLocked();
        return false;
    }
    // Возможно, повреждён — попробуем .bak
    const QString bak = vaultPath + QStringLiteral(".bak");
    if (QFile::exists(bak)) {
        QFile::remove(vaultPath);
        QFile::copy(bak, vaultPath);
        if (ex::crypto::VaultKey::unlock(vaultPath, QString(), kCredService, m_dataKey, err)) {
            EXLOG_WARN("vault") << "restored from .bak";
            m_vaultError.clear();
            return true;
        }
    }

    // Vault не открывается. Если НЕТ аккаунтов с сохранёнными сессиями —
    // терять нечего: пересоздаём с нуля. Это лечит «corrupted» после сбоев
    // на пустом хранилище.
    const bool hasAnySession = [this]() {
        for (const auto& a : m_store->accounts())
            if (a.hasSession)
                return true;
        return false;
    }();
    if (!hasAnySession) {
        EXLOG_WARN("vault") << "vault unusable (" << err << ") and no stored sessions — recreating";
        QFile::remove(vaultPath);
        QFile::remove(bak);
        QFile::remove(ex::crypto::VaultKey::deviceKeyFilePath(vaultPath));
        ex::crypto::VaultKey::deleteLegacyCredentialKey(kCredService);
        if (ex::crypto::VaultKey::create(vaultPath, QString(), kCredService)) {
            if (ex::crypto::VaultKey::unlock(vaultPath, QString(), kCredService, m_dataKey, err)) {
                EXLOG_INFO("vault") << "recreated device-mode vault";
                m_vaultError.clear();
                return true;
            }
        }
    }

    m_vaultError = err.isEmpty() ? QStringLiteral("corrupted") : err;
    emit vaultLocked();
    return false;
}

bool AppContext::vaultUnlocked()
{
    return m_vault.isUnlocked();
}

void AppContext::unlockWithPassword(const QString& password, bool& ok)
{
    const QString vaultPath = ex::util::AppPaths::dataDir() + kVaultFile;
    QString err;
    QByteArray key;
    if (ex::crypto::VaultKey::unlock(vaultPath, password, kCredService, key, err)) {
        m_dataKey = key;
        m_vault.setDataKey(key);
        m_vaultError.clear();
        ok = true;
    } else {
        m_vaultError = err;
        ok = false;
    }
}

bool AppContext::resetVault()
{
    const QString vaultPath = ex::util::AppPaths::dataDir() + kVaultFile;
    m_vault.setDataKey({});
    m_vault.clearAll();

    QFile::remove(vaultPath);
    QFile::remove(vaultPath + QStringLiteral(".bak"));
    QFile::remove(ex::crypto::VaultKey::deviceKeyFilePath(vaultPath));
    ex::crypto::VaultKey::deleteLegacyCredentialKey(kCredService);

    QString err;
    QByteArray key;
    const bool created = ex::crypto::VaultKey::create(vaultPath, QString(), kCredService)
        && ex::crypto::VaultKey::unlock(vaultPath, QString(), kCredService, key, err);
    if (!created) {
        m_vaultError = err.isEmpty() ? QStringLiteral("vault-create-failed") : err;
        EXLOG_ERROR("vault") << "reset failed:" << m_vaultError;
        return false;
    }

    m_dataKey = key;
    m_vault.setDataKey(key);
    m_vaultError.clear();

    // Ключ новый: старые блобы уже удалены, сессии помечаем истёкшими.
    for (auto account : m_store->accounts()) {
        account.hasSession = false;
        account.cookieExpired = true;
        m_store->updateAccount(account);
    }
    EXLOG_WARN("vault") << "vault reset — stored sessions invalidated";
    emit accountsChanged();
    return true;
}

} // namespace ex::ui

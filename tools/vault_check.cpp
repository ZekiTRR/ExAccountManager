// Диагностика хранилища секретов. Только локальные проверки: не делает
// сетевых запросов и не может инвалидировать сессию/ротировать cookie.
#include "core/crypto/envelope.h"
#include "core/store/account_store.h"
#include "core/store/database.h"
#include "core/store/vault.h"
#include "core/util/log.h"
#include "core/util/paths.h"
#include "platform/windows/dpapi.h"
#include "platform/windows/elevation.h"

#include <QCoreApplication>
#include <QFile>
#include <cstdio>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("ExAccountManager"));
    QCoreApplication::setOrganizationName(QStringLiteral("ExAccountManager"));
    ex::util::AppPaths::init();
    ex::log::init(ex::util::AppPaths::logsDir(), 7);

    std::printf("process elevated: %d\n", ex::platform::isElevated() ? 1 : 0);
    const QString service = QStringLiteral("ExAccountManager");
    const QString vaultPath = ex::util::AppPaths::dataDir() + QStringLiteral("/vault.dat");

    QFile vaultFile(vaultPath);
    std::printf("vault exists=%d size=%lld\n", int(vaultFile.exists()),
                static_cast<long long>(vaultFile.size()));

    const QString keyPath = ex::crypto::VaultKey::deviceKeyFilePath(vaultPath);
    QFile keyFile(keyPath);
    std::printf("device key file exists=%d size=%lld\n", int(keyFile.exists()),
                static_cast<long long>(keyFile.size()));

    bool dpapiProtOk = false;
    const QByteArray probe =
        ex::platform::dpapi::protect(QByteArrayLiteral("selftest"), &dpapiProtOk);
    bool dpapiUnprotOk = false;
    const QByteArray probeOut = ex::platform::dpapi::unprotect(probe, &dpapiUnprotOk);
    std::printf("dpapi self-test: protect=%d unprotect=%d roundtrip=%d\n",
                int(dpapiProtOk), int(dpapiUnprotOk),
                int(dpapiUnprotOk && probeOut == QByteArrayLiteral("selftest")));

    bool legacyOk = false;
    const QByteArray legacyKey =
        ex::crypto::VaultKey::legacyCredentialKey(service, &legacyOk);
    std::printf("legacy credential key: ok=%d size=%lld\n", int(legacyOk),
                static_cast<long long>(legacyKey.size()));

    const auto header = ex::crypto::VaultKey::readHeader(vaultPath);
    std::printf("header: bytes=%lld mode=%d\n", static_cast<long long>(header.bytes.size()),
                static_cast<int>(header.mode));

    ex::store::Database db;
    if (!db.open(ex::util::AppPaths::dbPath())) {
        std::printf("CANNOT OPEN DB\n");
        return 1;
    }

    QByteArray dataKey;
    QString err;
    const bool unlocked =
        ex::crypto::VaultKey::unlock(vaultPath, QString(), service, dataKey, err);
    std::printf("vault unlock: ok=%d err=%s\n", unlocked, qPrintable(err));
    if (!unlocked)
        return 1;

    ex::store::Vault vault(db);
    vault.setDataKey(dataKey);
    ex::store::AccountStore store(db);

    const auto accounts = store.accounts();
    std::printf("accounts in db: %lld\n", static_cast<long long>(accounts.size()));
    for (const auto& a : accounts) {
        const QString cookie = vault.cookie(a.id);
        const QString password = vault.password(a.id);
        std::printf(
            "account id=%lld user=%s hasSession=%d expired=%d cookieLen=%lld "
            "passwordLen=%lld lastValidated=%s\n",
            static_cast<long long>(a.id), qPrintable(a.username),
            a.hasSession ? 1 : 0, a.cookieExpired ? 1 : 0,
            static_cast<long long>(cookie.size()),
            static_cast<long long>(password.size()),
            qPrintable(a.lastValidated.toString(Qt::ISODate)));
    }
    return 0;
}

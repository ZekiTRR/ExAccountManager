#include "core/crypto/envelope.h"
#include "core/util/log.h"
#include "core/util/paths.h"
#include "ui/app_context.h"
#include "ui/main_window.h"
#include "ui/material/icons.h"
#include "ui/theme.h"
#include "ui/vault_dialog.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QMessageBox>
#include <QTimer>

#include <cstdio>
#include <windows.h>
#include <shellapi.h>

// Диагностика хранилища секретов (без UI): --check-vault [пароль]
static int checkVault(const QString& password)
{
    ex::util::AppPaths::init();
    ex::log::setConsoleEcho(true);
    const QString vaultPath = ex::util::AppPaths::dataDir() + QStringLiteral("/vault.dat");
    std::printf("vault path: %s\n", qPrintable(vaultPath));

    QFile f(vaultPath);
    std::printf("exists: %d, size: %lld\n", int(f.exists()), f.size());
    if (!f.open(QIODevice::ReadOnly)) {
        std::printf("CANNOT OPEN\n");
        return 1;
    }
    const QByteArray raw = f.read(98);
    std::printf("magic: %.8s\n", raw.constData());
    std::printf("version: %d, mode: %d\n", raw[8], raw[9]);

    const auto h = ex::crypto::VaultKey::readHeader(vaultPath);
    std::printf("header parsed: %lld bytes, mode %d\n",
                static_cast<long long>(h.bytes.size()), static_cast<int>(h.mode));

    QByteArray credKey;
    bool credOk = false;
    credKey = ex::crypto::VaultKey::legacyCredentialKey(QStringLiteral("ExAccountManager"), &credOk);
    std::printf("legacy key in cred manager: ok=%d size=%lld\n", credOk,
                static_cast<long long>(credKey.size()));

    QByteArray dataKey;
    QString err;
    const bool ok = ex::crypto::VaultKey::unlock(vaultPath, password,
                                                 QStringLiteral("ExAccountManager"), dataKey, err);
    std::printf("unlock: ok=%d err=%s dataKeySize=%lld\n", ok, qPrintable(err),
                static_cast<long long>(dataKey.size()));

    // Точка отказа напрямую ключом из cred manager
    if (!ok && credOk) {
        const bool ok2 = ex::crypto::VaultKey::unlockWithKey(vaultPath, credKey, dataKey, err);
        std::printf("unlockWithKey(cred-key): ok=%d err=%s\n", ok2, qPrintable(err));
    }
    return ok ? 0 : 1;
}

int main(int argc, char* argv[])
{
    // High-DPI (Qt6 включён по умолчанию, но явность не мешает)
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ExAccountManager"));
    QApplication::setOrganizationName(QStringLiteral("ExAccountManager"));
    QApplication::setApplicationVersion(QStringLiteral(EX_VERSION_STRING));

    // Внутренний de-elevated hand-off: запускается с токеном explorer и
    // открывает URI от имени обычного пользователя (см. openUriDeElevated).
    // Должно быть ДО single-instance mutex и инициализации контекста.
    {
        const QStringList rawArgs = QCoreApplication::arguments();
        const int idx = rawArgs.indexOf(QStringLiteral("--open-uri"));
        if (idx >= 0 && idx + 1 < rawArgs.size()) {
            const QString uri = rawArgs.at(idx + 1);
            // Только игровые URI: helper не должен быть универсальным
            // de-elevated лаунчером.
            if (!uri.startsWith(QStringLiteral("roblox-player:"), Qt::CaseInsensitive))
                return 1;
            const HINSTANCE r = ShellExecuteW(
                nullptr, L"open",
                reinterpret_cast<const wchar_t*>(uri.utf16()),
                nullptr, nullptr, SW_SHOWNORMAL);
            return reinterpret_cast<qintptr>(r) > 32 ? 0 : 1;
        }
    }

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("console"), QStringLiteral("echo logs to console")});
    parser.addOption({QStringLiteral("check-vault"), QStringLiteral("diagnose secret vault and exit")});
    parser.process(app);

    if (parser.isSet(QStringLiteral("check-vault"))) {
        const QStringList pos = parser.positionalArguments();
        return checkVault(pos.isEmpty() ? QString() : pos.first());
    }

    if (parser.isSet(QStringLiteral("console")))
        ex::log::setConsoleEcho(true);

    // --- single-instance: named mutex
    static HANDLE single = CreateMutexW(nullptr, TRUE, L"ExAccountManager_SingleInstance");
    if (single && GetLastError() == ERROR_ALREADY_EXISTS) {
        QMessageBox::information(nullptr, QStringLiteral("ExAccountManager"),
                                  QStringLiteral("Приложение уже запущено."));
        return 0;
    }

    // --- контекст: БД, vault, сервисы
    ex::ui::AppContext ctx;
    if (!ctx.init()) {
        QMessageBox::critical(nullptr, QStringLiteral("ExAccountManager"),
                              QStringLiteral("Не удалось инициализировать приложение (см. логи)."));
        return 1;
    }

    // --- тема: шрифт иконок → QSS + QPalette
    ex::ui::icons::init();
    const QString themeName = ctx.settings().get(QStringLiteral("general.theme")).toString();
    ex::ui::theme::applyTheme(themeName);
    QApplication::setWindowIcon(ex::ui::icons::appIcon());

    // --- хранилище секретов: пароль/восстановление до открытия главного окна
    ex::ui::VaultDialog::ensureUnlocked(ctx, nullptr);

    ex::ui::MainWindow w(ctx);
    if (ctx.settings().get(QStringLiteral("general.startMinimized")).toBool())
        w.hide(); // стартуем в трей
    else
        w.show();

    const int code = app.exec();

    ex::log::shutdown();
    if (single)
        ReleaseMutex(single);
    return code;
}

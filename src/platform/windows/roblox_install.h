#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

namespace ex::platform {

// Установленный клиент Roblox (папка Versions/version-*).
struct RobloxInstallation {
    QString exePath;
    QString folderName;
    QDateTime modified;
};

// Все найденные RobloxPlayerBeta.exe в стандартных Versions-каталогах
// (%LOCALAPPDATA%, Program Files, Program Files (x86)), новые сверху.
QList<RobloxInstallation> robloxInstallations();

// Что Windows запускает для roblox-player: ссылок (AssocQueryString).
// Пустая строка — ассоциация не зарегистрирована.
QString registeredRobloxPlayer();

// Разрешить выбранный путь: exe RobloxPlayerBeta.exe или папка (сама
// установка / Versions). Пустая строка, если клиент не найден.
QString resolveRobloxPlayerPath(const QString& configured);

// RobloxPlayerBeta.exe прямо в папке, иначе самый свежий из version-*.
QString findRobloxPlayerInFolder(const QString& folder);

} // namespace ex::platform

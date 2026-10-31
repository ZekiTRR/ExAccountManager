#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

namespace ex::platform {

// An installed Roblox client (Versions/version-* folder).
struct RobloxInstallation {
    QString exePath;
    QString folderName;
    QDateTime modified;
};

// Every RobloxPlayerBeta.exe found in the standard Versions folders
// (%LOCALAPPDATA%, Program Files, Program Files (x86)), newest first.
QList<RobloxInstallation> robloxInstallations();

// What Windows launches for roblox-player: links (AssocQueryString).
// An empty string means the association is not registered.
QString registeredRobloxPlayer();

// Resolve a chosen path: a RobloxPlayerBeta.exe or a folder (itself
// install / Versions). Empty string when no client is found.
QString resolveRobloxPlayerPath(const QString& configured);

// RobloxPlayerBeta.exe directly in the folder, else the newest version-*.
QString findRobloxPlayerInFolder(const QString& folder);

} // namespace ex::platform

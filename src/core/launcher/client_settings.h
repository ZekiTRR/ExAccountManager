#pragma once

#include <QJsonObject>
#include <QStringList>

namespace ex::launcher {

// ClientAppSettings.json (<Roblox Versions>\<version-*>\ClientSettings).
// The app no longer caps FPS; clearFpsCap() removes the flag
// set by older versions or by hand, so the client runs uncapped.
class ClientSettings {
public:
    static bool clearFpsCap();

    // Every version-* folder that contains a RobloxPlayerBeta.exe.
    static QStringList versionDirs();

private:
    static QString robloxVersionsRoot();
    static bool writeSettingsToDir(const QString& clientSettingsDir, const QJsonObject& obj);
    static QJsonObject readSettingsFromDir(const QString& clientSettingsDir);
};

} // namespace ex::launcher

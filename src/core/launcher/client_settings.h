#pragma once

#include <QJsonObject>
#include <QStringList>

namespace ex::launcher {

// ClientAppSettings.json (<Roblox Versions>\<version-*>\ClientSettings).
// FPS-лимит в приложении больше не используется; clearFpsCap() снимает ключ,
// выставленный прошлыми версиями/вручную, чтобы клиент работал без капа.
class ClientSettings {
public:
    static bool clearFpsCap();

    // Все папки version-*, где лежит RobloxPlayerBeta.exe.
    static QStringList versionDirs();

private:
    static QString robloxVersionsRoot();
    static bool writeSettingsToDir(const QString& clientSettingsDir, const QJsonObject& obj);
    static QJsonObject readSettingsFromDir(const QString& clientSettingsDir);
};

} // namespace ex::launcher

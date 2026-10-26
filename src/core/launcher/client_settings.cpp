#include "core/launcher/client_settings.h"

#include "core/util/fs_util.h"
#include "core/util/log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

#include <cstdlib>

namespace ex::launcher {

namespace {
constexpr const char* kBackupSuffix = ".exmanager-backup.json";
} // namespace

QString ClientSettings::robloxVersionsRoot()
{
    // %LOCALAPPDATA%\Roblox\Versions
    const QString local = QString::fromWCharArray(_wgetenv(L"LOCALAPPDATA"));
    return local + QStringLiteral("/Roblox/Versions");
}

QStringList ClientSettings::versionDirs()
{
    QStringList out;
    const QString root = robloxVersionsRoot();
    if (root.isEmpty())
        return out;
    const QFileInfoList entries = QDir(root)
        .entryInfoList({QStringLiteral("version-*")}, QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& d : entries) {
        if (QFile::exists(d.absoluteFilePath() + QStringLiteral("/RobloxPlayerBeta.exe")))
            out.append(d.absoluteFilePath());
    }
    return out;
}

QJsonObject ClientSettings::readSettingsFromDir(const QString& clientSettingsDir)
{
    const QString path = clientSettingsDir + QStringLiteral("/ClientAppSettings.json");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

bool ClientSettings::writeSettingsToDir(const QString& clientSettingsDir, const QJsonObject& obj)
{
    if (!ex::util::fs::ensureDir(clientSettingsDir))
        return false;
    const QString path = clientSettingsDir + QStringLiteral("/ClientAppSettings.json");

    // Бэкап оригинала (однократно)
    const QString backup = clientSettingsDir + QStringLiteral("/ClientAppSettings")
        + QLatin1String(kBackupSuffix);
    if (!QFile::exists(backup) && QFile::exists(path))
        QFile::copy(path, backup);

    const QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Indented);
    if (!ex::util::fs::writeAtomic(path, data))
        return false;

    // Readback-верификация (приём Astro)
    QFile check(path);
    if (!check.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject readback = QJsonDocument::fromJson(check.readAll()).object();
    return readback == obj;
}

bool ClientSettings::clearFpsCap()
{
    const QStringList dirs = versionDirs();
    bool all = true;
    for (const QString& dir : dirs) {
        const QString csDir = dir + QStringLiteral("/ClientSettings");
        QJsonObject obj = readSettingsFromDir(csDir);
        const QString fpsKey = QStringLiteral("DFIntTaskSchedulerTargetFps");
        const QString flagKey = QStringLiteral("FFlagTaskSchedulerLimitTargetFpsTo2402");
        if (!obj.contains(fpsKey) && !obj.contains(flagKey))
            continue;
        obj.remove(fpsKey);
        obj.remove(flagKey);
        all = writeSettingsToDir(csDir, obj) && all;
        EXLOG_INFO("client-settings") << "fps cap removed in" << dir;
    }
    return all;
}

} // namespace ex::launcher

#include "core/util/paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

namespace ex::util {

QString AppPaths::s_rootOverride;
bool AppPaths::s_portable = false;

void AppPaths::init()
{
    if (!s_rootOverride.isEmpty())
        return; // already initialised (test)

    // Portable mode: a "portable" marker file next to the exe
    const QString exeDir = QCoreApplication::applicationDirPath();
    if (QFile::exists(exeDir + QStringLiteral("/portable")))
        s_portable = true;

    QDir d(dataDir());
    if (!d.exists())
        d.mkpath(QStringLiteral("."));
    for (const char* sub : {"backups", "logs", "cache", "exports", "themes", "presets", "screenshots"})
        d.mkpath(QString::fromLatin1(sub));
}

bool AppPaths::portable() { return s_portable; }

QString AppPaths::dataDir()
{
    if (!s_rootOverride.isEmpty())
        return s_rootOverride;
    if (s_portable)
        return QCoreApplication::applicationDirPath() + QStringLiteral("/data");
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString AppPaths::dbPath() { return dataDir() + QStringLiteral("/exmanager.db"); }
QString AppPaths::backupsDir() { return dataDir() + QStringLiteral("/backups"); }
QString AppPaths::logsDir() { return dataDir() + QStringLiteral("/logs"); }
QString AppPaths::cacheDir() { return dataDir() + QStringLiteral("/cache"); }
QString AppPaths::exportsDir() { return dataDir() + QStringLiteral("/exports"); }
QString AppPaths::themesDir() { return dataDir() + QStringLiteral("/themes"); }
QString AppPaths::presetsDir() { return dataDir() + QStringLiteral("/presets"); }
QString AppPaths::screenshotsDir() { return dataDir() + QStringLiteral("/screenshots"); }

} // namespace ex::util

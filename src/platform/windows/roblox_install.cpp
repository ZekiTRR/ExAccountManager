#include "platform/windows/roblox_install.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>
#include <shlwapi.h>
#include <string>

namespace ex::platform {
namespace {

constexpr auto kPlayerExe = "RobloxPlayerBeta.exe";

QString normalizeKey(const QString& path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return (canonical.isEmpty() ? info.absoluteFilePath() : canonical).toLower();
}

QStringList versionsRoots()
{
    QStringList roots;
    const auto add = [&roots](const QString& base) {
        if (base.isEmpty())
            return;
        const QString root = QDir(base).filePath(QStringLiteral("Roblox/Versions"));
        if (!roots.contains(root, Qt::CaseInsensitive))
            roots.append(root);
    };

    QString local = qEnvironmentVariable("LOCALAPPDATA");
    if (local.isEmpty())
        local = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    add(local);
    add(qEnvironmentVariable("ProgramFiles"));
    add(qEnvironmentVariable("ProgramFiles(x86)"));
    return roots;
}

} // namespace

QList<RobloxInstallation> robloxInstallations()
{
    QList<RobloxInstallation> installs;
    QSet<QString> seen;

    for (const QString& root : versionsRoots()) {
        const QDir rootDir(root);
        if (!rootDir.exists())
            continue;
        const QFileInfoList entries = rootDir.entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
        for (const QFileInfo& entry : entries) {
            if (!entry.fileName().startsWith(QStringLiteral("version-"),
                                             Qt::CaseInsensitive)) {
                continue;
            }
            const QFileInfo exe(
                QDir(entry.absoluteFilePath()).filePath(QLatin1String(kPlayerExe)));
            if (!exe.isFile())
                continue;
            const QString key = normalizeKey(exe.absoluteFilePath());
            if (seen.contains(key))
                continue;
            seen.insert(key);
            installs.append({exe.absoluteFilePath(), entry.fileName(),
                             exe.lastModified()});
        }
    }

    // The handler may point at a custom build outside the standard
    // Versions (WEAO etc.) — show its RobloxPlayerBeta in the list.
    const QString registered = registeredRobloxPlayer();
    if (!registered.isEmpty()) {
        const QFileInfo handler(registered);
        QFileInfo exe(handler.absolutePath()
                      + QStringLiteral("/RobloxPlayerBeta.exe"));
        if (handler.isFile()
            && handler.fileName().compare(QStringLiteral("RobloxPlayerBeta.exe"),
                                          Qt::CaseInsensitive) == 0) {
            exe = handler;
        }
        if (exe.isFile()) {
            const QString key = normalizeKey(exe.absoluteFilePath());
            if (!seen.contains(key)) {
                seen.insert(key);
                installs.append({exe.absoluteFilePath(),
                                 exe.dir().dirName(), exe.lastModified()});
            }
        }
    }

    std::sort(installs.begin(), installs.end(),
              [](const RobloxInstallation& a, const RobloxInstallation& b) {
                  if (a.modified != b.modified)
                      return a.modified > b.modified;
                  return a.folderName < b.folderName;
              });
    return installs;
}

QString registeredRobloxPlayer()
{
    constexpr wchar_t kProtocol[] = L"roblox-player";
    DWORD length = 0;
    HRESULT hr = AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE,
                                   kProtocol, nullptr, nullptr, &length);
    if (FAILED(hr) || length == 0)
        return {};

    std::wstring buffer(length, L'\0');
    DWORD written = length;
    hr = AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, kProtocol, nullptr,
                           buffer.data(), &written);
    if (FAILED(hr) || written == 0)
        return {};

    QString path = QString::fromWCharArray(buffer.c_str()).trimmed();
    if (path.size() >= 2 && path.startsWith(QLatin1Char('"'))
        && path.endsWith(QLatin1Char('"'))) {
        path = path.mid(1, path.size() - 2).trimmed();
    }
    return path;
}

QString findRobloxPlayerInFolder(const QString& folder)
{
    const QFileInfo folderInfo(folder);
    if (folderInfo.isFile()) {
        return folderInfo.fileName().compare(QLatin1String(kPlayerExe),
                                             Qt::CaseInsensitive) == 0
            ? folderInfo.absoluteFilePath()
            : QString();
    }
    if (!folderInfo.isDir())
        return {};

    const QFileInfo direct(
        QDir(folderInfo.absoluteFilePath()).filePath(QLatin1String(kPlayerExe)));
    if (direct.isFile())
        return direct.absoluteFilePath();

    QList<QFileInfo> candidates;
    const QFileInfoList entries = QDir(folderInfo.absoluteFilePath())
        .entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    for (const QFileInfo& entry : entries) {
        if (!entry.fileName().startsWith(QStringLiteral("version-"),
                                         Qt::CaseInsensitive)) {
            continue;
        }
        const QFileInfo exe(
            QDir(entry.absoluteFilePath()).filePath(QLatin1String(kPlayerExe)));
        if (exe.isFile())
            candidates.append(exe);
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const QFileInfo& a, const QFileInfo& b) {
                  return a.lastModified() > b.lastModified();
              });
    return candidates.isEmpty() ? QString()
                                : candidates.first().absoluteFilePath();
}

QString resolveRobloxPlayerPath(const QString& configured)
{
    QString path = configured.trimmed();
    while (path.size() >= 2 && path.startsWith(QLatin1Char('"'))
           && path.endsWith(QLatin1Char('"'))) {
        path = path.mid(1, path.size() - 2).trimmed();
    }
    if (path.isEmpty())
        return {};

    const QFileInfo info(path);
    if (info.isFile()) {
        return info.fileName().compare(QLatin1String(kPlayerExe),
                                       Qt::CaseInsensitive) == 0
            ? info.absoluteFilePath()
            : QString();
    }
    if (info.isDir())
        return findRobloxPlayerInFolder(info.absoluteFilePath());
    return {};
}

} // namespace ex::platform

#include "core/util/fs_util.h"

#include "core/util/log.h"
#include "core/util/paths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <sodium.h>

namespace ex::util::fs {

bool writeAtomic(const QString& path, const QByteArray& data)
{
    // QSaveFile уже реализует tmp+rename атомарно; fsync делаем руками через flush()
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        log::warn("fs") << "writeAtomic: cannot open" << path << ":" << f.errorString();
        return false;
    }
    if (f.write(data) != data.size()) {
        f.cancelWriting();
        log::warn("fs") << "writeAtomic: short write" << path;
        return false;
    }
    if (!f.flush() || !f.commit()) {
        log::warn("fs") << "writeAtomic: commit failed" << path;
        return false;
    }
    return true;
}

QByteArray readFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

QString readTextFile(const QString& path)
{
    return QString::fromUtf8(readFile(path));
}

QString sha256Hex(const QByteArray& data)
{
    if (sodium_init() < 0)
        return {};
    unsigned char hash[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(hash,
                       reinterpret_cast<const unsigned char*>(data.constData()),
                       static_cast<unsigned long long>(data.size()));
    QByteArray hex(reinterpret_cast<const char*>(hash), sizeof hash);
    return QString::fromLatin1(hex.toHex());
}

QString sha256File(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    if (sodium_init() < 0)
        return {};
    crypto_hash_sha256_state st;
    crypto_hash_sha256_init(&st);
    char buf[64 * 1024];
    while (true) {
        const qint64 n = f.read(buf, sizeof buf);
        if (n < 0)
            return {};
        if (n == 0)
            break;
        crypto_hash_sha256_update(&st, reinterpret_cast<const unsigned char*>(buf),
                                  static_cast<unsigned long long>(n));
    }
    unsigned char hash[crypto_hash_sha256_BYTES];
    crypto_hash_sha256_final(&st, hash);
    QByteArray hex(reinterpret_cast<const char*>(hash), sizeof hash);
    return QString::fromLatin1(hex.toHex());
}

bool backupFile(const QString& path, const QString& backupsDir, int keep)
{
    if (!QFile::exists(path))
        return false;
    ensureDir(backupsDir);
    const QFileInfo info(path);
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString dest = backupsDir + QStringLiteral("/%1.%2.bak").arg(info.fileName(), stamp);
    if (!QFile::copy(path, dest)) {
        log::warn("fs") << "backup: copy failed" << path << "->" << dest;
        return false;
    }
    // Ротация: удалить самые старые сверх keep
    const QStringList entries = QDir(backupsDir)
        .entryList({info.fileName() + QStringLiteral(".*.bak")}, QDir::Files, QDir::Name);
    for (int i = 0; i < entries.size() - keep; ++i)
        QFile::remove(backupsDir + QLatin1Char('/') + entries[i]);
    return true;
}

bool restoreLatestBackup(const QString& path, const QString& backupsDir)
{
    const QFileInfo info(path);
    const QStringList entries = QDir(backupsDir)
        .entryList({info.fileName() + QStringLiteral(".*.bak")}, QDir::Files, QDir::Name);
    if (entries.isEmpty())
        return false;
    const QString latest = backupsDir + QLatin1Char('/') + entries.last();
    QFile::remove(path);
    if (!QFile::copy(latest, path))
        return false;
    log::warn("fs") << "restored from backup:" << latest;
    return true;
}

bool ensureDir(const QString& dir)
{
    if (dir.isEmpty())
        return false;
    return QDir().mkpath(dir);
}

} // namespace ex::util::fs

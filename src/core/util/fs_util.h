#pragma once

#include <QByteArray>
#include <QString>

namespace ex::util::fs {

// Atomic file write: tmp + flush + fsync + rename (over the old one).
// Returns false and logs on error. The "torn file" class is removed
// structurally — a reader sees either the old or the new content.
bool writeAtomic(const QString& path, const QByteArray& data);

QByteArray readFile(const QString& path);
QString readTextFile(const QString& path);

// SHA-256 of a file (empty string on error).
QString sha256File(const QString& path);
QString sha256Hex(const QByteArray& data);

// Backup: copies path -> backups/<name>.<timestamp>.bak, prunes older than keep.
bool backupFile(const QString& path, const QString& backupsDir, int keep = 10);

// Restore from the newest backup (when the main file is damaged).
bool restoreLatestBackup(const QString& path, const QString& backupsDir);

bool ensureDir(const QString& dir);

} // namespace ex::util::fs

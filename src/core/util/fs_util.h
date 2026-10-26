#pragma once

#include <QByteArray>
#include <QString>

namespace ex::util::fs {

// Атомарная запись файла: tmp + flush + fsync + rename (поверх старого).
// Возвращает false и пишет в лог при ошибке. Класс «порванный файл» устранён
// структурно — читатель либо видит старое содержимое, либо новое.
bool writeAtomic(const QString& path, const QByteArray& data);

QByteArray readFile(const QString& path);
QString readTextFile(const QString& path);

// SHA-256 файла (пустая строка при ошибке).
QString sha256File(const QString& path);
QString sha256Hex(const QByteArray& data);

// Бэкап: копирует path -> backups/<name>.<timestamp>.bak, чистит старее keep.
bool backupFile(const QString& path, const QString& backupsDir, int keep = 10);

// Восстановление из последнего бэкапа (при повреждении основного файла).
bool restoreLatestBackup(const QString& path, const QString& backupsDir);

bool ensureDir(const QString& dir);

} // namespace ex::util::fs

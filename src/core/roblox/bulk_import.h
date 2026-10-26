#pragma once

#include <QString>
#include <QStringList>
#include <QList>

namespace ex::roblox {

// Парсер bulk-импорта: понимает все форматы трёх менеджеров.
//
//   user:pass
//   user:pass:cookie
//   user::cookie
//   cookie (голый токен)
//   _|WARNING:-DO-NOT-SHARE-THIS...|<token>   (полное значение cookie)
//   Cookie: .ROBLOSECURITY=<value>; Path=/    (скопированный заголовок)
//   email:pass
//
// Разделители записей: перевод строки. CSV/TSV-поля (таб, запятая, ';')
// поддерживаются, если последнее поле — cookie. Комментарии: # и //.
struct BulkEntry {
    QString username;   // может быть пустым, если только cookie
    QString password;
    QString cookie;     // полное значение cookie (warning-префикс сохраняется)
};

class BulkImportParser {
public:
    static QList<BulkEntry> parse(const QString& text);

    // Разбор файла .txt/.csv/.tsv (кодировка UTF-8 / UTF-16 / Latin1 fallback).
    static QList<BulkEntry> parseFile(const QString& path);
};

} // namespace ex::roblox

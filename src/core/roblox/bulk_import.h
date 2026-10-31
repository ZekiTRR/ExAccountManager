#pragma once

#include <QString>
#include <QStringList>
#include <QList>

namespace ex::roblox {

// Bulk import parser: understands all formats of the three managers.
//
//   user:pass
//   user:pass:cookie
//   user::cookie
//   cookie (bare token)
//   _|WARNING:-DO-NOT-SHARE-THIS...|<token>   (full cookie value)
//   Cookie: .ROBLOSECURITY=<value>; Path=/    (copied header)
//   email:pass
//
// Record separator: newline. CSV/TSV fields (tab, comma, ';')
// are supported when the last field is a cookie. Comments: # and //.
struct BulkEntry {
    QString username;   // may be empty when only a cookie is given
    QString password;
    QString cookie;     // full cookie value (the warning prefix is kept)
};

class BulkImportParser {
public:
    static QList<BulkEntry> parse(const QString& text);

    // Parse a .txt/.csv/.tsv file (UTF-8 / UTF-16, Latin1 fallback).
    static QList<BulkEntry> parseFile(const QString& path);
};

} // namespace ex::roblox

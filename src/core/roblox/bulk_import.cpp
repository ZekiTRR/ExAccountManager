#include "core/roblox/bulk_import.h"

#include "core/util/redact.h"

#include <QFile>
#include <QRegularExpression>
#include <QStringDecoder>

namespace ex::roblox {

namespace {

QString stripTrailingColons(QString value)
{
    while (value.endsWith(QLatin1Char(':')))
        value.chop(1);
    return value;
}

// "user:pass" (a password may contain ':') -> record fields.
bool splitUserPass(const QString& prefix, BulkEntry& entry)
{
    const QString p = stripTrailingColons(prefix.trimmed());
    const int colon = p.indexOf(QLatin1Char(':'));
    if (colon <= 0)
        return false;
    const QString user = p.left(colon).trimmed();
    const QString pass = p.mid(colon + 1).trimmed();
    if (user.isEmpty() || pass.isEmpty())
        return false;
    entry.username = user;
    entry.password = pass;
    return true;
}

bool isHexString(const QString& s)
{
    static const QRegularExpression hex{QStringLiteral(R"(^[0-9A-Fa-f]+$)")};
    return !s.isEmpty() && hex.match(s).hasMatch();
}

QString decodeFile(const QByteArray& raw)
{
    // BOMs: UTF-8, UTF-16 LE/BE. No BOM = UTF-8; on hard errors Latin-1.
    if (raw.startsWith(QByteArrayLiteral("\xEF\xBB\xBF")))
        return QStringDecoder(QStringDecoder::Utf8).decode(raw.mid(3));
    if (raw.startsWith(QByteArrayLiteral("\xFF\xFE")))
        return QStringDecoder(QStringDecoder::Utf16LE).decode(raw.mid(2));
    if (raw.startsWith(QByteArrayLiteral("\xFE\xFF")))
        return QStringDecoder(QStringDecoder::Utf16BE).decode(raw.mid(2));

    QStringDecoder utf8(QStringDecoder::Utf8);
    QString text = utf8.decode(raw);
    if (utf8.hasError())
        text = QString::fromLatin1(raw);
    return text;
}

} // namespace

QList<BulkEntry> BulkImportParser::parse(const QString& text)
{
    QList<BulkEntry> out;

    // One line = one record. Inside a line CSV/TSV separators are allowed
    // (RAM/Astro format port), but a cookie value never contains them.
    const QStringList lines = text.split(
        QRegularExpression(QStringLiteral("\r\n|\n|\r")), Qt::SkipEmptyParts);

    for (QString line : lines) {
        line = line.trimmed();
        // Invisible first-line markers
        while (!line.isEmpty() && line.front().unicode() == 0xFEFF)
            line.remove(0, 1);
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))
            || line.startsWith(QStringLiteral("//")))
            continue;

        // 0) A line may hold several comma-separated user:pass pairs
        // (Astro): do not split a CSV row that belongs to one record.
        if (!line.contains(QLatin1String("_|WARNING:"), Qt::CaseInsensitive)
            && line.contains(QLatin1Char(',')) && !line.contains(QLatin1Char('\t'))) {
            const QStringList parts = line.split(QLatin1Char(','));
            bool allColonRecords = parts.size() > 1;
            for (const QString& part : parts) {
                if (!part.contains(QLatin1Char(':'))) {
                    allColonRecords = false;
                    break;
                }
            }
            if (allColonRecords) {
                for (const QString& part : parts) {
                    const QList<BulkEntry> sub = parse(part.trimmed());
                    for (const BulkEntry& entry : sub)
                        out.append(entry);
                }
                continue;
            }
        }

        // 1) Self-documenting banner format. Everything before the banner
        // may be "user:pass:" — parsed as credentials. But when
        // a Cookie:/Set-Cookie header precedes the banner, it is not a login.
        const int banner = line.indexOf(QStringLiteral("_|WARNING:"), 0, Qt::CaseInsensitive);
        if (banner >= 0) {
            BulkEntry e;
            const QString prefix = line.left(banner);
            e.cookie = util::redact::normalizeCookieValue(line.mid(banner));
            const bool headerLike =
                prefix.contains(QStringLiteral("ROBLOSECURITY"), Qt::CaseInsensitive)
                || line.startsWith(QStringLiteral("Cookie:"), Qt::CaseInsensitive)
                || line.startsWith(QStringLiteral("Set-Cookie:"), Qt::CaseInsensitive);
            if (!headerLike && !prefix.trimmed().isEmpty()) {
                if (!splitUserPass(prefix, e)) {
                    // user::<cookie> — login without a password (Astro format)
                    const QString user = stripTrailingColons(prefix.trimmed()).trimmed();
                    if (!user.isEmpty() && !user.contains(QLatin1Char(':')))
                        e.username = user;
                }
            }
            if (!e.cookie.isEmpty())
                out.append(e);
            continue;
        }

        // 2) A copied Cookie:/Set-Cookie header or a name=value pair.
        if (line.contains(QStringLiteral("ROBLOSECURITY"), Qt::CaseInsensitive)
            || line.startsWith(QStringLiteral("Cookie:"), Qt::CaseInsensitive)
            || line.startsWith(QStringLiteral("Set-Cookie:"), Qt::CaseInsensitive)) {
            BulkEntry e;
            e.cookie = util::redact::normalizeCookieValue(line);
            if (util::redact::looks_like_roblox_cookie(e.cookie))
                out.append(e);
            continue;
        }

        // 3) CSV/TSV: user<TAB>pass<TAB>cookie, user,pass,cookie, etc.
        const QStringList fields = line.split(
            QRegularExpression(QStringLiteral("[\t,;]")), Qt::SkipEmptyParts);
        if (fields.size() >= 2
            && util::redact::looks_like_roblox_cookie(fields.last())) {
            BulkEntry e;
            e.cookie = util::redact::normalizeCookieValue(fields.last());
            if (fields.size() >= 3) {
                e.username = fields.at(0).trimmed();
                e.password = fields.at(1).trimmed();
            } else {
                e.username = fields.at(0).trimmed();
            }
            if (!e.cookie.isEmpty())
                out.append(e);
            continue;
        }
        if (fields.size() == 2 && !line.contains(QLatin1Char(':'))) {
            BulkEntry e;
            e.username = fields.at(0).trimmed();
            e.password = fields.at(1).trimmed();
            if (!e.username.isEmpty() && !e.password.isEmpty())
                out.append(e);
            continue;
        }

        // 4) Bare cookie token (long base64/hex string).
        if (util::redact::looks_like_roblox_cookie(line)) {
            BulkEntry e;
            e.cookie = util::redact::normalizeCookieValue(line);
            out.append(e);
            continue;
        }

        // 5) user:pass[:cookie]. A banner-less cookie is the long tail after ':',
        // one that does not look like a plain password (isHex/looks_like heuristic).
        const int firstColon = line.indexOf(QLatin1Char(':'));
        if (firstColon <= 0)
            continue;
        const int lastColon = line.lastIndexOf(QLatin1Char(':'));
        if (lastColon > firstColon) {
            const QString tail = line.mid(lastColon + 1).trimmed();
            if (util::redact::looks_like_roblox_cookie(tail)
                || (tail.size() >= 60 && isHexString(tail))) {
                BulkEntry e;
                e.username = line.left(firstColon).trimmed();
                e.password = line.mid(firstColon + 1, lastColon - firstColon - 1).trimmed();
                e.cookie = util::redact::normalizeCookieValue(tail);
                if (!e.cookie.isEmpty())
                    out.append(e);
                continue;
            }
        }
        BulkEntry e;
        e.username = line.left(firstColon).trimmed();
        e.password = line.mid(firstColon + 1).trimmed();
        if (!e.username.isEmpty() && !e.password.isEmpty())
            out.append(e);
    }
    return out;
}

QList<BulkEntry> BulkImportParser::parseFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QString text = decodeFile(f.readAll());
    return parse(text);
}

} // namespace ex::roblox

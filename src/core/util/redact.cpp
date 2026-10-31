#include "core/util/redact.h"

#include <QRegularExpression>
#include <QStringBuilder>

namespace ex::util::redact {

namespace {

// .ROBLOSECURITY cookie: "_|WARNING:...|<token>". Since 2026 Roblox hands out different
// banner variants and tokens holding ".", "+", "/", "=" — so the token
// is described as "everything up to a space/quote", not a narrow character class.
const QRegularExpression kCookiePattern{
    QStringLiteral(R"(_\|WARNING:[^|\r\n]*\|[^\s"'<>]{16,})"),
    QRegularExpression::CaseInsensitiveOption};

// Assignments like ".ROBLOSECURITY=<token>" inside a Cookie-/Set-Cookie header.
const QRegularExpression kCookieAssign{
    QStringLiteral(R"((\.ROBLOSECURITY\s*[=:]\s*"?)([^\s;"'<>]+))"),
    QRegularExpression::CaseInsensitiveOption};

// Auth ticket in a header or in a URI (gameinfo:)
const QRegularExpression kTicket{
    QStringLiteral(R"((rbx-authentication-ticket|gameinfo|authorization|bearer)\s*[=:]\s*[][(){}<>"' A-Za-z0-9_%+.\-]{20,})"),
    QRegularExpression::CaseInsensitiveOption};

// CSRF token
const QRegularExpression kCsrf{
    QStringLiteral(R"((x-csrf-token)\s*[=:]\s*[][(){}<>"' A-Za-z0-9_%+.\-]{16,})"),
    QRegularExpression::CaseInsensitiveOption};

// Windows user path: C:\Users\<name>\...
const QRegularExpression kUserPath{
    QStringLiteral(R"((?:[A-Za-z]:\\Users\\)[^\\/:*?\"<>|\s]+)"),
    QRegularExpression::CaseInsensitiveOption};

// E-mail — personal data, so it is masked in diagnostics
const QRegularExpression kEmail{
    QStringLiteral(R"(\b[A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,}\b)")};

// Long hex/base64-looking strings (generic secrets, >= 32 chars)
const QRegularExpression kLongSecret{
    QStringLiteral(R"(\b[A-Fa-f0-9]{64,}\b|\b[A-Za-z0-9+/]{64}={0,2}\b)")};

// The Cookie:/Set-Cookie: label at the start of a pasted line.
const QRegularExpression kHeaderLabel{
    QStringLiteral(R"(^\s*(?:set-)?cookie\s*:\s*)"),
    QRegularExpression::CaseInsensitiveOption};

// Cookie-name assignment inside a header (it may not come first).
const QRegularExpression kCookieAssignment{
    QStringLiteral(R"((?:^|[;\s])\.?ROBLOSECURITY\s*=\s*)"),
    QRegularExpression::CaseInsensitiveOption};

const QRegularExpression kBareCookie{
    QStringLiteral(R"(^[A-Za-z0-9_%+\-/=.]{30,}$)")};

} // namespace

QString scrub(const QString& text)
{
    if (text.isEmpty())
        return text;
    QString out = text;
    // Order matters: the most specific shapes first
    out.replace(kCookiePattern, QStringLiteral("<roblox-cookie>"));
    out.replace(kCookieAssign, QStringLiteral("\\1<redacted>"));
    out.replace(kCsrf, QStringLiteral("\\1=<redacted>"));
    out.replace(kTicket, QStringLiteral("\\1=<redacted>"));
    out.replace(kUserPath, QStringLiteral("<user-path>"));
    out.replace(kEmail, QStringLiteral("<email>"));
    out.replace(kLongSecret, QStringLiteral("<secret>"));
    return out;
}

bool looks_like_roblox_cookie(const QString& text)
{
    const QString t = normalizeCookieValue(text);
    if (t.isEmpty())
        return false;
    if (t.startsWith(QStringLiteral("_|WARNING:"), Qt::CaseInsensitive)) {
        const int bar = t.lastIndexOf(QLatin1Char('|'));
        return bar >= 0 && t.size() - bar - 1 >= 8;
    }
    if (t.contains(QLatin1Char(':')) || t.contains(QLatin1Char(' ')))
        return false;
    return kBareCookie.match(t).hasMatch();
}

QString normalizeCookieValue(const QString& text)
{
    const auto stripQuotes = [](QString value) {
        // Quotes around the value are not part of it; inner characters
        // ('.', '+', '/', '=' and the like) are left alone.
        while (value.size() >= 2) {
            const QChar first = value.front();
            const QChar last = value.back();
            if ((first == QLatin1Char('"') && last == QLatin1Char('"'))
                || (first == QLatin1Char('\'') && last == QLatin1Char('\''))) {
                value = value.mid(1, value.size() - 2).trimmed();
            } else {
                break;
            }
        }
        return value;
    };

    QString s = stripQuotes(text.trimmed());
    if (s.isEmpty())
        return {};

    // A whole header was pasted: "Cookie: ..." / "Set-Cookie: ..."
    s.remove(kHeaderLabel);
    s = stripQuotes(s.trimmed());

    // "name=.ROBLOSECURITY; .ROBLOSECURITY=<value>; Path=/; ..." -> value only.
    const auto assign = kCookieAssignment.match(s);
    if (assign.hasMatch()) {
        s = s.mid(assign.capturedEnd()).trimmed();
        const int semi = s.indexOf(QLatin1Char(';'));
        if (semi >= 0)
            s = s.left(semi).trimmed();
        s = stripQuotes(s);
    } else {
        // Bare value: ';' can only come from copied attributes.
        const int semi = s.indexOf(QLatin1Char(';'));
        if (semi >= 0)
            s = s.left(semi).trimmed();
    }

    // If junk precedes the self-documenting banner (for instance
    // "user:pass:"), we keep the value starting at the banner.
    const int banner = s.indexOf(QStringLiteral("_|WARNING:"), 0, Qt::CaseInsensitive);
    if (banner > 0)
        s = stripQuotes(s.mid(banner).trimmed());

    return s;
}

} // namespace ex::util::redact

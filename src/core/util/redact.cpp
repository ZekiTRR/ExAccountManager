#include "core/util/redact.h"

#include <QRegularExpression>
#include <QStringBuilder>

namespace ex::util::redact {

namespace {

// .ROBLOSECURITY cookie: "_|WARNING:...|<token>". С 2026 Roblox выдаёт разные
// варианты баннера и токены, содержащие ".", "+", "/", "=" — поэтому токен
// описывается как «всё до пробела/кавычки», а не узким классом символов.
const QRegularExpression kCookiePattern{
    QStringLiteral(R"(_\|WARNING:[^|\r\n]*\|[^\s"'<>]{16,})"),
    QRegularExpression::CaseInsensitiveOption};

// Присвоения вида ".ROBLOSECURITY=<token>" (в Cookie-/Set-Cookie-заголовке).
const QRegularExpression kCookieAssign{
    QStringLiteral(R"((\.ROBLOSECURITY\s*[=:]\s*"?)([^\s;"'<>]+))"),
    QRegularExpression::CaseInsensitiveOption};

// Auth ticket в заголовке или в URI (gameinfo:)
const QRegularExpression kTicket{
    QStringLiteral(R"((rbx-authentication-ticket|gameinfo|authorization|bearer)\s*[=:]\s*[][(){}<>"' A-Za-z0-9_%+.\-]{20,})"),
    QRegularExpression::CaseInsensitiveOption};

// CSRF-токен
const QRegularExpression kCsrf{
    QStringLiteral(R"((x-csrf-token)\s*[=:]\s*[][(){}<>"' A-Za-z0-9_%+.\-]{16,})"),
    QRegularExpression::CaseInsensitiveOption};

// Путь пользователя Windows: C:\Users\<name>\...
const QRegularExpression kUserPath{
    QStringLiteral(R"((?:[A-Za-z]:\\Users\\)[^\\/:*?\"<>|\s]+)"),
    QRegularExpression::CaseInsensitiveOption};

// Email — считается персоналией, маскируется в диагностике
const QRegularExpression kEmail{
    QStringLiteral(R"(\b[A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,}\b)")};

// Длинные hex/base64-подобные строки (секреты общего вида, >= 32 симв.)
const QRegularExpression kLongSecret{
    QStringLiteral(R"(\b[A-Fa-f0-9]{64,}\b|\b[A-Za-z0-9+/]{64}={0,2}\b)")};

// Метка Cookie:/Set-Cookie: в начале вставленной строки.
const QRegularExpression kHeaderLabel{
    QStringLiteral(R"(^\s*(?:set-)?cookie\s*:\s*)"),
    QRegularExpression::CaseInsensitiveOption};

// Присвоение имени cookie внутри заголовка (может быть не первым).
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
    // Порядок важен: сначала самые специфичные формы
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
        // Кавычки вокруг значения не являются его частью. Внутренние символы
        // ('.', '+', '/', '=' и т.п.) не трогаем.
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

    // Вставлен целый заголовок: "Cookie: ..." / "Set-Cookie: ..."
    s.remove(kHeaderLabel);
    s = stripQuotes(s.trimmed());

    // "name=.ROBLOSECURITY; .ROBLOSECURITY=<value>; Path=/; ..." -> только значение.
    const auto assign = kCookieAssignment.match(s);
    if (assign.hasMatch()) {
        s = s.mid(assign.capturedEnd()).trimmed();
        const int semi = s.indexOf(QLatin1Char(';'));
        if (semi >= 0)
            s = s.left(semi).trimmed();
        s = stripQuotes(s);
    } else {
        // Голое значение: ';' может появиться только от скопированных атрибутов.
        const int semi = s.indexOf(QLatin1Char(';'));
        if (semi >= 0)
            s = s.left(semi).trimmed();
    }

    // Если перед самодокументируемым баннером оказался мусор (например,
    // "user:pass:"), оставляем значение начиная с баннера.
    const int banner = s.indexOf(QStringLiteral("_|WARNING:"), 0, Qt::CaseInsensitive);
    if (banner > 0)
        s = stripQuotes(s.mid(banner).trimmed());

    return s;
}

} // namespace ex::util::redact

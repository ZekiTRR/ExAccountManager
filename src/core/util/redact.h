#pragma once

#include <QString>
#include <QStringList>

namespace ex::util {

// Скраббинг секретов в тексте перед логированием/отправкой.
// Backstop-подход (как у RM): каждый лог-вызов проходит через scrub(),
// поэтому секрет, случайно попавший в любое сообщение, будет замаскирован,
// даже если точечные фильтры забыли.
namespace redact {

// Маскирует известные формы секретов:
//  - cookie .ROBLOSECURITY:  _|WARNING:-DO-NOT-SHARE-THIS.--Sharing...|<token>
//  - присвоение cookie:      .ROBLOSECURITY=..., cookie=...
//  - auth ticket:            rbx-authentication-ticket: <...>, gameinfo:<...>
//  - csrf:                   x-csrf-token: <...>
//  - bearer-токены:          Bearer <...>, Authorization: ...
//  - путь пользователя:      C:\Users\<name>\ -> C:\Users\<redacted>\
//  - emails (для диагностик): user@host -> <email>
QString scrub(const QString& text);

// True, если строка похожа на cookie Roblox (для валидации ввода).
bool looks_like_roblox_cookie(const QString& text);

// Нормализация ПОЛНОГО значения cookie. Префикс предупреждения
// "_|WARNING:-DO-NOT-SHARE-THIS.--Sharing...|_TOKEN" — это ЧАСТЬ значения,
// без него Roblox отвечает 401. Возвращает значение целиком:
//   "_|WARNING:...|_TOKEN" -> как есть (целиком)
//   голый длинный токен    -> как есть
//   мусор/кавычки/пробелы  -> очищенная строка
QString normalizeCookieValue(const QString& text);

} // namespace redact
} // namespace ex::util

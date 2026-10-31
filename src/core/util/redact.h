#pragma once

#include <QString>
#include <QStringList>

namespace ex::util {

// Scrub secrets from text before logging or sending it.
// Backstop approach (like RM): every log call goes through scrub(),
// so a secret that slips into any message gets masked,
// even when a spot filter was forgotten.
namespace redact {

// Masks the known secret shapes:
//  - cookie .ROBLOSECURITY:  _|WARNING:-DO-NOT-SHARE-THIS.--Sharing...|<token>
//  - cookie assignment:      .ROBLOSECURITY=..., cookie=...
//  - auth ticket:            rbx-authentication-ticket: <...>, gameinfo:<...>
//  - csrf:                   x-csrf-token: <...>
//  - bearer tokens:          Bearer <...>, Authorization: ...
//  - user path:              C:\Users\<name>\ -> C:\Users\<redacted>\
//  - emails (in diagnostics): user@host -> <email>
QString scrub(const QString& text);

// True when the string looks like a Roblox cookie (input validation).
bool looks_like_roblox_cookie(const QString& text);

// Normalise the FULL cookie value. The warning prefix
// "_|WARNING:-DO-NOT-SHARE-THIS.--Sharing...|_TOKEN" is PART of the value,
// without it Roblox answers 401. Returns the whole value:
//   "_|WARNING:...|_TOKEN" -> used as-is (whole value)
//   bare long token           -> used as-is
//   junk/quotes/whitespace     -> cleaned string
QString normalizeCookieValue(const QString& text);

} // namespace redact
} // namespace ex::util

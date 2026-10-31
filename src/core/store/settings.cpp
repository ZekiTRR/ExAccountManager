#include "core/store/settings.h"

#include "core/util/log.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>

namespace ex::store {

namespace {
const QRegularExpression kSensitive{
    QStringLiteral("password|cookie|token|secret|session|authorization|api_key|roblosecurity"),
    QRegularExpression::CaseInsensitiveOption};

// A scalar is stored as {"v": ...}: QJsonDocument::fromVariant rejects bare
// QString (silently yields a null document).
QByteArray encodeValue(const QVariant& value)
{
    QJsonObject o;
    o[QStringLiteral("v")] = QJsonValue::fromVariant(value);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QVariant decodeValue(const QByteArray& raw)
{
    return QJsonDocument::fromJson(raw).object().value(QStringLiteral("v")).toVariant();
}
} // namespace

QVariant Settings::get(const QString& key, const QVariant& def) const
{
    Stmt s(m_db.handle(), QStringLiteral("SELECT value FROM settings WHERE key=?"));
    s.bind(1, key);
    if (s.step())
        return decodeValue(s.colBlob(0));
    return m_defaults.contains(key) ? m_defaults.value(key) : def;
}

void Settings::set(const QString& key, const QVariant& value)
{
    if (isSensitiveKey(key)) {
        EXLOG_WARN("settings") << "refusing to store sensitive key:" << key;
        return;
    }
    const QByteArray raw = encodeValue(value);
    Stmt s(m_db.handle(), QStringLiteral(
        "INSERT INTO settings (key, value) VALUES (?,?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value"));
    s.bind(1, key);
    s.bind(2, raw);
    s.exec();
}

QVariantHash Settings::section(const QString& prefix) const
{
    QVariantHash out;
    const QString p = prefix.endsWith(QLatin1Char('.')) ? prefix : prefix + QLatin1Char('.');
    Stmt s(m_db.handle(), QStringLiteral("SELECT key, value FROM settings WHERE key LIKE ?"));
    s.bind(1, p + QStringLiteral("%"));
    while (s.step()) {
        const QString full = s.colText(0);
        const QString sub = full.mid(p.size());
        if (sub.isEmpty() || sub.contains(QLatin1Char('.')))
            continue; // one level only
        out.insert(sub, decodeValue(s.colBlob(1)));
    }
    // defaults under this prefix
    for (auto it = m_defaults.constBegin(); it != m_defaults.constEnd(); ++it)
        if (it.key().startsWith(p) && !out.contains(it.key().mid(p.size())))
            out.insert(it.key().mid(p.size()), it.value());
    return out;
}

void Settings::registerDefaults(const QVariantHash& defaults)
{
    for (auto it = defaults.constBegin(); it != defaults.constEnd(); ++it)
        m_defaults.insert(it.key(), it.value());
}

bool Settings::isSensitiveKey(const QString& key)
{
    return kSensitive.match(key).hasMatch();
}

QVariantHash Settings::all() const
{
    QVariantHash out;
    Stmt s(m_db.handle(), QStringLiteral("SELECT key, value FROM settings"));
    while (s.step())
        out.insert(s.colText(0), decodeValue(s.colBlob(1)));
    for (auto it = m_defaults.constBegin(); it != m_defaults.constEnd(); ++it)
        if (!out.contains(it.key()))
            out.insert(it.key(), it.value());
    return out;
}

} // namespace ex::store

#pragma once

#include "core/store/database.h"

#include <QVariantHash>

namespace ex::store {

// Settings: key-value with typed defaults and recursive merge.
// Guard: keys with secret names (cookie/password/token/secret/...)
// are rejected — secrets live only in the vault (Astro culture).
class Settings {
public:
    explicit Settings(Database& db) : m_db(db) {}

    QVariant get(const QString& key, const QVariant& def = {}) const;
    void set(const QString& key, const QVariant& value);

    // Setting block "general.launch" -> hash of every "general.launch.*".
    QVariantHash section(const QString& prefix) const;

    // Default values (registered up front; get returns them when unset).
    void registerDefaults(const QVariantHash& defaults);

    // Unsafe key name (secret) -> true. A setter with that name will throw.
    static bool isSensitiveKey(const QString& key);

    // Snapshot of every setting (debugging; structurally secret-free).
    QVariantHash all() const;

private:
    Database& m_db;
    QVariantHash m_defaults;
};

} // namespace ex::store

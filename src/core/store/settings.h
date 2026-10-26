#pragma once

#include "core/store/database.h"

#include <QVariantHash>

namespace ex::store {

// Настройки: key-value с типизированными дефолтами и рекурсивным merge.
// Guard: ключи с секретными именами (cookie/password/token/secret/...)
// отклоняются — секреты живут только в Vault (культура Astro).
class Settings {
public:
    explicit Settings(Database& db) : m_db(db) {}

    QVariant get(const QString& key, const QVariant& def = {}) const;
    void set(const QString& key, const QVariant& value);

    // Блок настроек "general.launch" -> hash всех "general.launch.*".
    QVariantHash section(const QString& prefix) const;

    // Значения по умолчанию (регистрируются заранее; get вернёт их при отсутствии).
    void registerDefaults(const QVariantHash& defaults);

    // Небезопасное имя ключа (секрет) -> true. Сеттер с таким именем упадёт.
    static bool isSensitiveKey(const QString& key);

    // Снимок всех настроек (для отладки; секретных там нет по построению).
    QVariantHash all() const;

private:
    Database& m_db;
    QVariantHash m_defaults;
};

} // namespace ex::store

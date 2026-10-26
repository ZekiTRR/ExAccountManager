#pragma once

#include <QByteArray>

namespace ex::crypto {

// Envelope-шифрование (по мотивам формата RAMSTORE из RM).
//
// Файл vault.dat:
//   off  size  поле
//   0    8     magic "EXSTORE1"
//   8    1     version = 2
//   9    1     mode: 0 = device (ключ в Credential Manager), 1 = password
//   10   16    salt (password mode; в device mode — нули)
//   26   12    wrap nonce
//   38   48    wrapped data key (32 байта ключа + 16 тег AES-GCM)
//   86   12    body nonce
//   98   ..    зашифрованное тело (AAD = все 98 байт заголовка)
//
// Свойства:
//  - Смена пароля/режима = rekey: перезаписываются только 98 байт заголовка,
//    секреты в БД (зашифрованные data key) не трогаются.
//  - AAD связывает mode/salt/nonces с телом: подмена байта режима не
//    даунгрейдит и не подменяет хранилище.
//  - Body содержит JSON с версией формата и настройками vault.

class VaultKey {
public:
    enum class Mode { Device = 0, Password = 1 };

    struct Header {
        static constexpr int kSize = 98;
        QByteArray bytes; // полный заголовок
        Mode mode = Mode::Device;
        QByteArray salt;      // 16
        QByteArray wrapNonce; // 12
        QByteArray wrappedKey;// 48
        QByteArray bodyNonce; // 12
    };

    // Создаёт новое хранилище. password пустой => device mode.
    static bool create(const QString& vaultPath, const QString& password,
                       const QString& service /* Credential Manager service */);

    // Загружает заголовок, не расшифровывая ключ.
    static Header readHeader(const QString& vaultPath);

    // Разблокировка: выводит data key в памяти.
    // device mode: ключ достаётся из Credential Manager.
    // password mode: Argon2id(password, salt).
    // lastError: "wrong-password" | "corrupted" | "device-key-missing" | "io"
    static bool unlock(const QString& vaultPath, const QString& password,
                       const QString& service, QByteArray& outDataKey, QString& lastError);

    // Проверка без разблокировки: совместим ли пароль.
    static bool unlockWithKey(const QString& vaultPath, const QByteArray& wrappingKey,
                              QByteArray& outDataKey, QString& lastError);

    // Смена режима/пароля. Работает только при разблокированном dataKey.
    static bool rekey(const QString& vaultPath, const QByteArray& dataKey,
                      const QString& newPassword, const QString& service);

    // --- AEAD-примитивы для отдельных секретов (строки vault в БД) ---
    // nonce(12) + ciphertext+tag; AAD = контекст (имя ключа/строки).
    static QByteArray sealSecret(const QByteArray& dataKey, const QByteArray& aad, const QByteArray& plain);
    static QByteArray openSecret(const QByteArray& dataKey, const QByteArray& aad, const QByteArray& sealed, bool* ok = nullptr);

    // Argon2id (параметры как у RM: 19 MiB, t=3, p=1).
    static QByteArray deriveArgon2id(const QString& password, const QByteArray& salt);

    // --- ключи Credential Manager ---
    // --- ключ устройства (DPAPI-файл рядом с vault) ---
    // Файл <vaultPath>.key хранит data-ключ под DPAPI CurrentUser: работает
    // в любом контексте одного пользователя (в т.ч. elevated), в отличие от
    // Credential Manager, который изолирован по контекстам запуска.
    static QString deviceKeyFilePath(const QString& vaultPath);
    static bool storeDeviceKeyFile(const QString& vaultPath, const QByteArray& key);
    static QByteArray deviceKeyFromFile(const QString& vaultPath, bool* ok);

    // Legacy: ключ из Credential Manager (старые vault-файлы до миграции).
    static QByteArray legacyCredentialKey(const QString& service, bool* ok);
    static bool deleteLegacyCredentialKey(const QString& service);
};

} // namespace ex::crypto

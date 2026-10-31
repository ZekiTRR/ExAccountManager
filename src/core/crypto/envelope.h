#pragma once

#include <QByteArray>

namespace ex::crypto {

// Envelope encryption (modelled on the RAMSTORE format from RM).
//
// vault.dat layout:
//   off  size  field
//   0    8     magic "EXSTORE1"
//   8    1     version = 2
//   9    1     mode: 0 = device (key in a DPAPI .key file), 1 = password
//   10   16    salt (password mode; zeros in device mode)
//   26   12    wrap nonce
//   38   48    wrapped data key (32-byte key + 16-byte AES-GCM tag)
//   86   12    body nonce
//   98   ..    encrypted body (AAD = all 98 header bytes)
//
// Properties:
//  - Changing the password/mode is a rekey: only the 98 header bytes are
//    rewritten; secrets in the DB (encrypted with the data key) are untouched.
//  - The AAD binds mode/salt/nonces to the body: flipping the mode byte can
//    neither downgrade nor swap the vault.
//  - The body holds JSON with the format version and vault settings.

class VaultKey {
public:
    enum class Mode { Device = 0, Password = 1 };

    struct Header {
        static constexpr int kSize = 98;
        QByteArray bytes; // full header
        Mode mode = Mode::Device;
        QByteArray salt;      // 16
        QByteArray wrapNonce; // 12
        QByteArray wrappedKey;// 48
        QByteArray bodyNonce; // 12
    };

    // Creates a new vault. Empty password => device mode.
    static bool create(const QString& vaultPath, const QString& password,
                       const QString& service /* Credential Manager service */);

    // Reads the header without decrypting the key.
    static Header readHeader(const QString& vaultPath);

    // Unlock: yields the data key in memory.
    // device mode: the key comes from the DPAPI file (legacy fallback: Credential Manager).
    // password mode: Argon2id(password, salt).
    // lastError: "wrong-password" | "password-required" | "corrupted" |
    //            "device-key-missing" | "crypto-unavailable" | "io"
    static bool unlock(const QString& vaultPath, const QString& password,
                       const QString& service, QByteArray& outDataKey, QString& lastError);

    // Unwraps the data key with an already prepared wrapping key
    // (no password derivation or device key lookup).
    static bool unlockWithKey(const QString& vaultPath, const QByteArray& wrappingKey,
                              QByteArray& outDataKey, QString& lastError);

    // Changes mode/password. Requires an already unlocked dataKey.
    static bool rekey(const QString& vaultPath, const QByteArray& dataKey,
                      const QString& newPassword, const QString& service);

    // --- AEAD primitives for individual secrets (vault rows in the DB) ---
    // nonce(12) + ciphertext+tag; AAD = context (key/row name).
    static QByteArray sealSecret(const QByteArray& dataKey, const QByteArray& aad, const QByteArray& plain);
    static QByteArray openSecret(const QByteArray& dataKey, const QByteArray& aad, const QByteArray& sealed, bool* ok = nullptr);

    // Argon2id (parameters match RM: 19 MiB, t=3, p=1).
    static QByteArray deriveArgon2id(const QString& password, const QByteArray& salt);

    // --- device key (DPAPI file next to the vault) ---
    // The file <vaultPath>.key stores the data key under DPAPI CurrentUser: it
    // works in any context of the same user (including elevated), unlike
    // Credential Manager, which is isolated per launch context.
    static QString deviceKeyFilePath(const QString& vaultPath);
    static bool storeDeviceKeyFile(const QString& vaultPath, const QByteArray& key);
    static QByteArray deviceKeyFromFile(const QString& vaultPath, bool* ok);

    // Legacy: key from Credential Manager (vault files created before the migration).
    static QByteArray legacyCredentialKey(const QString& service, bool* ok);
    static bool deleteLegacyCredentialKey(const QString& service);
};

} // namespace ex::crypto

#include "core/crypto/envelope.h"
#include "core/crypto/sodium.h"
#include "core/util/fs_util.h"
#include "core/util/log.h"
#include "platform/windows/credential.h"
#include "platform/windows/dpapi.h"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <sodium.h>

namespace ex::crypto {

namespace {

constexpr char kMagic[] = "EXSTORE1";
constexpr int kHeaderSize = VaultKey::Header::kSize;

static_assert(crypto_aead_aes256gcm_KEYBYTES == 32);
static_assert(crypto_aead_aes256gcm_NPUBBYTES == 12);
static_assert(crypto_aead_aes256gcm_ABYTES == 16);

struct Aead {
    // AES-256-GCM; on x64 with AES-NI (every modern CPU) it is hardware
    // accelerated. sodium_init() must run before the check: without it the
    // detection does not see AES-NI and any decryption after a restart would
    // fail with "aes-ni-unavailable".
    static bool available()
    {
        return ensureSodium() && crypto_aead_aes256gcm_is_available() != 0;
    }

    static QByteArray seal(const QByteArray& key, const QByteArray& nonce,
                           const QByteArray& aad, const QByteArray& plain, QString* err = nullptr)
    {
        if (!available()) {
            if (err) *err = QStringLiteral("aes-ni-unavailable");
            return {};
        }
        QByteArray out(plain.size() + crypto_aead_aes256gcm_ABYTES, Qt::Uninitialized);
        unsigned long long outLen = 0;
        if (crypto_aead_aes256gcm_encrypt(
                reinterpret_cast<unsigned char*>(out.data()), &outLen,
                reinterpret_cast<const unsigned char*>(plain.constData()),
                static_cast<unsigned long long>(plain.size()),
                reinterpret_cast<const unsigned char*>(aad.constData()),
                static_cast<unsigned long long>(aad.size()),
                nullptr, // nsec
                reinterpret_cast<const unsigned char*>(nonce.constData()),
                reinterpret_cast<const unsigned char*>(key.constData())) != 0) {
            if (err) *err = QStringLiteral("encrypt-failed");
            return {};
        }
        out.resize(static_cast<int>(outLen));
        return out;
    }

    static QByteArray open(const QByteArray& key, const QByteArray& nonce,
                           const QByteArray& aad, const QByteArray& sealed, QString* err = nullptr)
    {
        if (!available()) {
            if (err) *err = QStringLiteral("aes-ni-unavailable");
            return {};
        }
        if (sealed.size() < crypto_aead_aes256gcm_ABYTES) {
            if (err) *err = QStringLiteral("ciphertext-too-short");
            return {};
        }
        QByteArray out(sealed.size() - crypto_aead_aes256gcm_ABYTES, Qt::Uninitialized);
        unsigned long long outLen = 0;
        if (crypto_aead_aes256gcm_decrypt(
                reinterpret_cast<unsigned char*>(out.data()), &outLen,
                nullptr,
                reinterpret_cast<const unsigned char*>(sealed.constData()),
                static_cast<unsigned long long>(sealed.size()),
                reinterpret_cast<const unsigned char*>(aad.constData()),
                static_cast<unsigned long long>(aad.size()),
                reinterpret_cast<const unsigned char*>(nonce.constData()),
                reinterpret_cast<const unsigned char*>(key.constData())) != 0) {
            if (err) *err = QStringLiteral("auth-failed");
            return {};
        }
        out.resize(static_cast<int>(outLen));
        return out;
    }
};

QByteArray randomBytes(int n)
{
    QByteArray b(n, Qt::Uninitialized);
    crypto::randomBytes(reinterpret_cast<unsigned char*>(b.data()), static_cast<size_t>(n));
    return b;
}

VaultKey::Header parseHeader(const QByteArray& raw)
{
    VaultKey::Header h;
    h.bytes = raw.left(kHeaderSize);
    if (raw.size() < kHeaderSize)
        return h;
    h.mode = static_cast<VaultKey::Mode>(static_cast<quint8>(raw[9]));
    h.salt = raw.mid(10, 16);
    h.wrapNonce = raw.mid(26, 12);
    h.wrappedKey = raw.mid(38, 48);
    h.bodyNonce = raw.mid(86, 12);
    return h;
}

QByteArray buildHeader(VaultKey::Mode mode, const QByteArray& salt,
                       const QByteArray& wrapNonce, const QByteArray& wrappedKey,
                       const QByteArray& bodyNonce)
{
    QByteArray h(kHeaderSize, 0);
    memcpy(h.data(), kMagic, 8);
    h[8] = 2; // version
    h[9] = static_cast<char>(static_cast<quint8>(static_cast<int>(mode)));
    memcpy(h.data() + 10, salt.constData(), 16);
    memcpy(h.data() + 26, wrapNonce.constData(), 12);
    memcpy(h.data() + 38, wrappedKey.constData(), 48);
    memcpy(h.data() + 86, bodyNonce.constData(), 12);
    return h;
}

QByteArray wrapAad(const QByteArray& header)
{
    // Binds magic/version/mode/salt/wrapNonce to the wrapped key: flipping the
    // mode byte will not let the key be unwrapped another way.
    return header.left(38); // everything before wrappedKey
}

} // namespace

// ------------------------------------------------------------------ public API

QString VaultKey::deviceKeyFilePath(const QString& vaultPath)
{
    return vaultPath + QStringLiteral(".key");
}

bool VaultKey::storeDeviceKeyFile(const QString& vaultPath, const QByteArray& key)
{
    // Key under DPAPI CurrentUser: any context of the same user can decrypt it
    // (including elevated) — unlike Credential Manager.
    bool ok = false;
    const QByteArray blob = platform::dpapi::protect(key, &ok);
    if (!ok || blob.isEmpty())
        return false;
    if (!ex::util::fs::writeAtomic(deviceKeyFilePath(vaultPath), blob))
        return false;
    // Readback verification (guards against races, an RM technique)
    bool ok2 = false;
    const QByteArray check = platform::dpapi::unprotect(
        ex::util::fs::readFile(deviceKeyFilePath(vaultPath)), &ok2);
    return ok2 && check == key;
}

QByteArray VaultKey::deviceKeyFromFile(const QString& vaultPath, bool* ok)
{
    const QByteArray blob = ex::util::fs::readFile(deviceKeyFilePath(vaultPath));
    if (blob.isEmpty()) {
        if (ok) *ok = false;
        return {};
    }
    return platform::dpapi::unprotect(blob, ok);
}

QByteArray VaultKey::legacyCredentialKey(const QString& service, bool* ok)
{
    QByteArray out;
    const bool r = platform::cred::read(service, QStringLiteral("device-key"), out);
    if (ok) *ok = r;
    return out;
}

bool VaultKey::deleteLegacyCredentialKey(const QString& service)
{
    return platform::cred::remove(service, QStringLiteral("device-key"));
}

bool VaultKey::create(const QString& vaultPath, const QString& password,
                      const QString& service)
{
    Q_UNUSED(service); // device key lives in the DPAPI file <vault>.key
    if (!ensureSodium() || !Aead::available()) {
        EXLOG_ERROR("vault") << "crypto unavailable";
        return false;
    }

    const QByteArray dataKey = randomBytes(32);
    const QByteArray bodyNonce = randomBytes(12);
    QByteArray salt(16, 0);
    QByteArray wrappingKey;

    Mode mode = Mode::Device;
    if (password.isEmpty()) {
        wrappingKey = randomBytes(32);
        if (!storeDeviceKeyFile(vaultPath, wrappingKey)) {
            EXLOG_ERROR("vault") << "cannot store device key (dpapi)";
            return false;
        }
    } else {
        mode = Mode::Password;
        salt = randomBytes(16);
        wrappingKey = deriveArgon2id(password, salt);
        if (wrappingKey.isEmpty())
            return false;
    }

    const QByteArray wrapNonce = randomBytes(12);
    const QByteArray protoHeader = buildHeader(mode, salt, wrapNonce, QByteArray(48, 0), bodyNonce);
    const QByteArray wrappedKey = Aead::seal(wrappingKey, wrapNonce, wrapAad(protoHeader), dataKey);
    if (wrappedKey.isEmpty())
        return false;

    const QByteArray header = buildHeader(mode, salt, wrapNonce, wrappedKey, bodyNonce);
    QJsonObject bodyObj;
    bodyObj[QStringLiteral("v")] = 2;
    bodyObj[QStringLiteral("created")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    const QByteArray sealedBody = Aead::seal(dataKey, bodyNonce, header,
                                             QJsonDocument(bodyObj).toJson(QJsonDocument::Compact));
    if (sealedBody.isEmpty())
        return false;

    if (!ex::util::fs::writeAtomic(vaultPath, header + sealedBody)) {
        EXLOG_ERROR("vault") << "cannot write vault file";
        return false;
    }
    return true;
}

VaultKey::Header VaultKey::readHeader(const QString& vaultPath)
{
    Header h;
    QFile f(vaultPath);
    if (!f.open(QIODevice::ReadOnly))
        return h;
    const QByteArray raw = f.read(kHeaderSize);
    if (raw.size() < kHeaderSize || memcmp(raw.constData(), kMagic, 8) != 0) {
        EXLOG_WARN("vault") << "bad vault magic";
        return h;
    }
    return parseHeader(raw);
}

bool VaultKey::unlock(const QString& vaultPath, const QString& password,
                      const QString& service, QByteArray& outDataKey, QString& lastError)
{
    if (!ensureSodium()) {
        lastError = QStringLiteral("crypto-unavailable");
        return false;
    }
    const Header h = readHeader(vaultPath);
    if (h.bytes.size() < kHeaderSize) {
        lastError = QStringLiteral("corrupted");
        return false;
    }
    QByteArray wrappingKey;
    if (h.mode == Mode::Device) {
        // Primary path: the DPAPI file <vault>.key
        bool ok = false;
        wrappingKey = deviceKeyFromFile(vaultPath, &ok);
        if (!ok || wrappingKey.size() != 32) {
            // Legacy: key from Credential Manager (vaults created before the migration).
            // If that is missing too, this device has "lost" the key.
            wrappingKey = legacyCredentialKey(service, &ok);
            if (!ok || wrappingKey.size() != 32) {
                lastError = QStringLiteral("device-key-missing");
                return false;
            }
        }
    } else {
        if (password.isEmpty()) {
            lastError = QStringLiteral("password-required");
            return false;
        }
        wrappingKey = deriveArgon2id(password, h.salt);
        if (wrappingKey.isEmpty()) {
            lastError = QStringLiteral("corrupted");
            return false;
        }
    }
    return unlockWithKey(vaultPath, wrappingKey, outDataKey, lastError);
}

bool VaultKey::unlockWithKey(const QString& vaultPath, const QByteArray& wrappingKey,
                             QByteArray& outDataKey, QString& lastError)
{
    if (!ensureSodium()) {
        lastError = QStringLiteral("crypto-unavailable");
        return false;
    }
    const Header h = readHeader(vaultPath);
    if (h.bytes.size() < kHeaderSize) {
        lastError = QStringLiteral("corrupted");
        return false;
    }
    QString err;
    const QByteArray dataKey = Aead::open(wrappingKey, h.wrapNonce, wrapAad(h.bytes),
                                          h.wrappedKey, &err);
    if (dataKey.isEmpty() || dataKey.size() != 32) {
        EXLOG_WARN("vault") << "key unwrap failed:" << err
                            << "mode" << static_cast<int>(h.mode)
                            << "wrappingKey" << wrappingKey.size();
        lastError = (h.mode == Mode::Password) ? QStringLiteral("wrong-password")
                                                       : QStringLiteral("corrupted");
        return false;
    }
    // Verify body integrity with the same key (full unlock)
    QFile f(vaultPath);
    if (!f.open(QIODevice::ReadOnly)) {
        lastError = QStringLiteral("io");
        return false;
    }
    f.seek(kHeaderSize);
    const QByteArray sealedBody = f.readAll();
    if (Aead::open(dataKey, h.bodyNonce, h.bytes, sealedBody, &err).isEmpty()) {
        EXLOG_WARN("vault") << "body auth failed:" << err << "size" << sealedBody.size();
        lastError = QStringLiteral("corrupted");
        return false;
    }
    outDataKey = dataKey;
    return true;
}

bool VaultKey::rekey(const QString& vaultPath, const QByteArray& dataKey,
                     const QString& newPassword, const QString& service)
{
    Q_UNUSED(service);
    if (!ensureSodium()) {
        EXLOG_ERROR("vault") << "crypto unavailable";
        return false;
    }
    const Header h = readHeader(vaultPath);
    if (h.bytes.size() < kHeaderSize)
        return false;

    QByteArray salt(16, 0);
    QByteArray wrappingKey;
    Mode mode = Mode::Device;
    if (newPassword.isEmpty()) {
        wrappingKey = randomBytes(32);
        if (!storeDeviceKeyFile(vaultPath, wrappingKey))
            return false;
    } else {
        mode = Mode::Password;
        salt = randomBytes(16);
        wrappingKey = deriveArgon2id(newPassword, salt);
        if (wrappingKey.isEmpty())
            return false;
    }

    const QByteArray wrapNonce = randomBytes(12);
    const QByteArray bodyNonce = randomBytes(12);
    const QByteArray protoHeader = buildHeader(mode, salt, wrapNonce, QByteArray(48, 0), bodyNonce);
    const QByteArray wrappedKey = Aead::seal(wrappingKey, wrapNonce, wrapAad(protoHeader), dataKey);
    if (wrappedKey.isEmpty())
        return false;

    const QByteArray header = buildHeader(mode, salt, wrapNonce, wrappedKey, bodyNonce);
    QJsonObject bodyObj;
    bodyObj[QStringLiteral("v")] = 2;
    bodyObj[QStringLiteral("rekeyed")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    const QByteArray sealedBody = Aead::seal(dataKey, bodyNonce, header,
                                             QJsonDocument(bodyObj).toJson(QJsonDocument::Compact));
    if (sealedBody.isEmpty())
        return false;

    if (!ex::util::fs::writeAtomic(vaultPath, header + sealedBody))
        return false;
    // Rewrite the .bak with the new contents so the old password cannot open
    // the backup (an RM technique)
    QFile::copy(vaultPath, vaultPath + QStringLiteral(".bak"));
    return true;
}

QByteArray VaultKey::sealSecret(const QByteArray& dataKey, const QByteArray& aad, const QByteArray& plain)
{
    if (!ensureSodium())
        return {};
    const QByteArray nonce = randomBytes(12);
    const QByteArray sealed = Aead::seal(dataKey, nonce, aad, plain);
    if (sealed.isEmpty())
        return {};
    return nonce + sealed;
}

QByteArray VaultKey::openSecret(const QByteArray& dataKey, const QByteArray& aad,
                                const QByteArray& sealed, bool* ok)
{
    if (!ensureSodium() || sealed.size() < 12 + crypto_aead_aes256gcm_ABYTES) {
        if (ok) *ok = false;
        return {};
    }
    const QByteArray nonce = sealed.left(12);
    QString err;
    const QByteArray plain = Aead::open(dataKey, nonce, aad, sealed.mid(12), &err);
    if (ok) *ok = !plain.isEmpty();
    return plain;
}

QByteArray VaultKey::deriveArgon2id(const QString& password, const QByteArray& salt)
{
    if (!ensureSodium())
        return {};
    if (salt.size() != crypto_pwhash_argon2id_SALTBYTES)
        return {};
    QByteArray key(crypto_aead_aes256gcm_KEYBYTES, Qt::Uninitialized);
    const QByteArray pw = password.toUtf8();
    // RM parameters: 19 MiB, opslimit 3, parallelism 1 (Argon2id v1.3).
    if (crypto_pwhash_argon2id(
            reinterpret_cast<unsigned char*>(key.data()),
            static_cast<unsigned long long>(key.size()),
            pw.constData(), static_cast<unsigned long long>(pw.size()),
            reinterpret_cast<const unsigned char*>(salt.constData()),
            3, 19 * 1024 * 1024, crypto_pwhash_argon2id_ALG_ARGON2ID13) != 0) {
        return {};
    }
    return key;
}

} // namespace ex::crypto

// Core basics: redaction, bulk parser, URI builder, envelope crypto,
// store CRUD. QtTest.
#include <QtTest>

#include "core/crypto/envelope.h"
#include "core/launcher/launch_uri.h"
#include "core/launcher/multi_roblox.h"
#include "core/net/websocket.h"
#include "core/roblox/bulk_import.h"
#include "core/roblox/cdp_login.h"
#include "core/roblox/http.h"
#include "core/store/account_store.h"
#include "core/store/database.h"
#include "core/store/settings.h"
#include "core/store/vault.h"
#include "core/util/redact.h"
#include "platform/windows/ram_limits.h"
#include "platform/windows/roblox_install.h"

#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QNetworkRequest>
#include <QProcess>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include <memory>

using namespace ex;

namespace {
struct CookieFixture {
    const QString cookie = QStringLiteral("_|WARNING:-DO-NOT-SHARE-THIS.--Sharing-this-cookie-will-compromise-your-account-DO-NOT-SHARE.|AABBCCDDEEFF00112233445566778899aabbccddeeff00112233445566778899AABBCCDDEEFF00112233445566778899aabbccddeeff00");

    // New 2026 format: different banner, GgIQAQ. marker and a hex token.
    const QString newBanner = QStringLiteral(
        "_|WARNING:-DO-NOT-SHARE-THIS.--Sharing-this-will-allow-someone-to-log-in-as-you-and-to-steal-your-ROBUX-and-items.|_");
    const QString newCookie = newBanner + QStringLiteral("GgIQAQ.")
        + QStringLiteral("A1B2C3D4E5F6").repeated(10);

    // Bare base64url token with characters that used to be cut off.
    const QString bareToken = QStringLiteral("AbC+def/Ghi=")
        + QString(QLatin1Char('x')).repeated(100);
};
} // namespace

class TestCore : public QObject {
    Q_OBJECT

private slots:
    void redactCookie() {
        CookieFixture f;
        const QString msg = QStringLiteral("login failed cookie=%1").arg(f.cookie);
        const QString scrubbed = util::redact::scrub(msg);
        QVERIFY(!scrubbed.contains(f.cookie.mid(200, 40)));
    }
    void redactTicket() {
        const QString msg = QStringLiteral("rbx-authentication-ticket: abc123def456ghi789jkl012mno345");
        QVERIFY(!util::redact::scrub(msg).contains(QLatin1String("abc123def456")));
    }
    void redactUserPath() {
        const QString msg = QStringLiteral("log at C:\\Users\\SecretUser\\AppData");
        QVERIFY(!util::redact::scrub(msg).contains(QLatin1String("SecretUser")));
    }
    void extractCookie() {
        CookieFixture f;
        // Full value: the WARNING prefix is PART of the cookie, never cut off
        const QString prefix = QStringLiteral("_|WARNING:-DO-NOT-SHARE-THIS.--Sharing-this-cookie-will-compromise-your-account-DO-NOT-SHARE.|_");
        const QString token = QString(QLatin1Char('A')).repeated(220);
        const QString full = prefix + token;
        QCOMPARE(util::redact::normalizeCookieValue(full), full);
        QVERIFY(util::redact::looks_like_roblox_cookie(full));

        // A bare token is kept as-is
        QCOMPARE(util::redact::normalizeCookieValue(token), token);
        QVERIFY(util::redact::looks_like_roblox_cookie(token));

        // Quotes/whitespace are stripped, the value is kept whole
        QCOMPARE(util::redact::normalizeCookieValue(
                     QStringLiteral("  \"%1\"  \n").arg(full)),
                 full);

        // New format: the GgIQAQ. marker and its dot must not cut the value
        QCOMPARE(util::redact::normalizeCookieValue(f.newCookie), f.newCookie);
        QVERIFY(util::redact::looks_like_roblox_cookie(f.newCookie));

        // A copied Set-Cookie header
        QCOMPARE(util::redact::normalizeCookieValue(
                     QStringLiteral("Set-Cookie: .ROBLOSECURITY=%1; Path=/; Secure; HttpOnly")
                         .arg(f.newCookie)),
                 f.newCookie);

        // Cookie header carrying foreign cookies before/after
        QCOMPARE(util::redact::normalizeCookieValue(
                     QStringLiteral("Cookie: foo=bar; .ROBLOSECURITY=%1; baz=qux")
                         .arg(f.newCookie)),
                 f.newCookie);

        // Bare base64 with '+' '/' '=' is preserved completely
        QCOMPARE(util::redact::normalizeCookieValue(f.bareToken), f.bareToken);
        QVERIFY(util::redact::looks_like_roblox_cookie(f.bareToken));
    }

    void bulkParse() {
        CookieFixture f;
        const QString longCookie = QStringLiteral("A").repeated(150);
        const QString input = QStringLiteral(
            "user1:pass1\n"
            "# comment\n"
            "user2:pass2:%1\n"
            "%1\n"
            "// comment2\n"
            "user3:pass:with:colons\n"
            "user4:pass4:%2\n"
            "Set-Cookie: .ROBLOSECURITY=%2; Path=/; Secure\n")
            .arg(longCookie, f.newCookie);
        const auto entries = roblox::BulkImportParser::parse(input);
        QCOMPARE(entries.size(), 6);
        QCOMPARE(entries[0].username, QStringLiteral("user1"));
        QCOMPARE(entries[0].password, QStringLiteral("pass1"));
        QCOMPARE(entries[1].cookie, longCookie);
        QCOMPARE(entries[2].cookie, longCookie);
        QCOMPARE(entries[3].password, QStringLiteral("pass:with:colons"));
        QCOMPARE(entries[4].username, QStringLiteral("user4"));
        QCOMPARE(entries[4].password, QStringLiteral("pass4"));
        QCOMPARE(entries[4].cookie, f.newCookie);
        QCOMPARE(entries[5].cookie, f.newCookie);
        // A header line must not become an account called "Set-Cookie".
        QVERIFY(entries[5].username.isEmpty());
        QVERIFY(entries[5].password.isEmpty());

        // user:pass:cookie holding a full warning value (previously the whole line
        // was mistaken for a cookie and lost its login/password)
        const auto withCreds = roblox::BulkImportParser::parse(
            QStringLiteral("user5:pass5:%1").arg(f.newCookie));
        QCOMPARE(withCreds.size(), 1);
        QCOMPARE(withCreds[0].username, QStringLiteral("user5"));
        QCOMPARE(withCreds[0].password, QStringLiteral("pass5"));
        QCOMPARE(withCreds[0].cookie, f.newCookie);

        // user::cookie — login without a password (Astro format)
        const auto noPassword = roblox::BulkImportParser::parse(
            QStringLiteral("user6::%1").arg(f.newCookie));
        QCOMPARE(noPassword.size(), 1);
        QCOMPARE(noPassword[0].username, QStringLiteral("user6"));
        QVERIFY(noPassword[0].password.isEmpty());
        QCOMPARE(noPassword[0].cookie, f.newCookie);

        // Cookie header on its own line
        const auto headerOnly = roblox::BulkImportParser::parse(
            QStringLiteral("Cookie: .ROBLOSECURITY=%1; Path=/").arg(f.newCookie));
        QCOMPARE(headerOnly.size(), 1);
        QCOMPARE(headerOnly[0].cookie, f.newCookie);

        // Several comma-separated user:pass pairs (Astro port)
        const auto commaList = roblox::BulkImportParser::parse(
            QStringLiteral("alpha:one,beta:two"));
        QCOMPARE(commaList.size(), 2);
        QCOMPARE(commaList[0].username, QStringLiteral("alpha"));
        QCOMPARE(commaList[1].username, QStringLiteral("beta"));
    }

    void launchUri() {
        model::LaunchTarget t;
        t.kind = model::LaunchTarget::Kind::Job;
        t.placeId = 12345;
        t.jobId = QStringLiteral("abc-def");
        const QString uri = launcher::LaunchUri::build(
            t, QStringLiteral("TICKET+WITH/SPECIAL="),
            QStringLiteral("111222333"), 123456789LL);
        // The ticket is percent-encoded: otherwise '+' splits URI fields.
        QVERIFY(uri.startsWith(QLatin1String(
            "roblox-player:1+launchmode:play+gameinfo:TICKET%2BWITH%2FSPECIAL%3D")));
        QVERIFY(uri.contains(QLatin1String("launchtime:111222333")));
        // placelauncherurl is one percent-encoded URL (RM/Astro form).
        QVERIFY(uri.contains(QLatin1String(
            "placelauncherurl:https%3A%2F%2Fwww.roblox.com%2Fgame%2FPlaceLauncher.ashx%3F"
            "request%3DRequestGameJob%26browserTrackerId%3D123456789%26placeId%3D12345%26"
            "gameId%3Dabc-def%26joinAttemptId%3D")));
        QVERIFY(uri.contains(QLatin1String("+browsertrackerid:123456789")));

        model::LaunchTarget place;
        place.kind = model::LaunchTarget::Kind::Place;
        place.placeId = 42;
        const QString placeUri = launcher::LaunchUri::build(
            place, QStringLiteral("T"), QStringLiteral("7"), 100000001LL);
        QVERIFY(placeUri.contains(QLatin1String(
            "RequestGame%26browserTrackerId%3D100000001%26placeId%3D42%26"
            "isPlayTogetherGame%3Dfalse")));

        model::LaunchTarget vip;
        vip.kind = model::LaunchTarget::Kind::PrivateServer;
        vip.placeId = 42;
        vip.privateServerCode = QStringLiteral("AbC-123_link");
        const QString vipUri = launcher::LaunchUri::build(
            vip, QStringLiteral("T"), QStringLiteral("7"), 100000001LL);
        QVERIFY(vipUri.contains(QLatin1String(
            "RequestPrivateGame%26browserTrackerId%3D100000001%26placeId%3D42%26"
            "linkCode%3DAbC-123_link")));
    }

    void parsePlaceInput() {
        QCOMPARE(launcher::LaunchUri::parsePlaceInput(QStringLiteral("5315046213")), qint64(5315046213));
        QCOMPARE(launcher::LaunchUri::parsePlaceInput(
                     QStringLiteral("https://www.roblox.com/games/9876/Some-Game")),
                 qint64(9876));
        QString code;
        launcher::LaunchUri::parsePlaceInput(
            QStringLiteral("https://www.roblox.com/games/555/Game?privateServerLinkCode=abc123"), &code);
        QCOMPARE(code, QStringLiteral("abc123"));

        // Share link: no placeId, only an invite code.
        const auto share = launcher::LaunchUri::parseInput(
            QStringLiteral("https://www.roblox.com/share?code=AbCd1234&type=Server"));
        QCOMPARE(share.placeId, qint64(0));
        QCOMPARE(share.shareCode, QStringLiteral("AbCd1234"));

        // Real VIP links: share?code=... and games/...?privateServerLinkCode=...
        const auto shareReal = launcher::LaunchUri::parseInput(QStringLiteral(
            "https://www.roblox.com/share?code=3d0ad9cc027d5f498c127271ab55fb51&type=Server"));
        QCOMPARE(shareReal.placeId, qint64(0));
        QCOMPARE(shareReal.shareCode,
                 QStringLiteral("3d0ad9cc027d5f498c127271ab55fb51"));

        const auto gamesVip = launcher::LaunchUri::parseInput(QStringLiteral(
            "https://www.roblox.com/games/2809202155/Your-Bizarre-Adventure"
            "?privateServerLinkCode=29617444843983806962063418628413"));
        QCOMPARE(gamesVip.placeId, qint64(2809202155));
        QCOMPARE(gamesVip.privateServerCode,
                 QStringLiteral("29617444843983806962063418628413"));

        // roblox:// from the web client
        const auto deeplink = launcher::LaunchUri::parseInput(
            QStringLiteral("roblox://experiences/start?placeId=777&gameInstanceId=job-1"));
        QCOMPARE(deeplink.placeId, qint64(777));
        QCOMPARE(deeplink.jobId, QStringLiteral("job-1"));

        // VIP link: both placeId and code.
        const auto vip = launcher::LaunchUri::parseInput(
            QStringLiteral("https://www.roblox.com/games/555/Game?privateServerLinkCode=abc123"));
        QCOMPARE(vip.placeId, qint64(555));
        QCOMPARE(vip.privateServerCode, QStringLiteral("abc123"));
    }

    void envelopeRoundtrip() {
        QTemporaryDir dir;
        const QString vaultPath = dir.path() + QStringLiteral("/vault.dat");
        const QString service = QStringLiteral("ExAccountManagerTest");

        // Device-mode creation
        QVERIFY(crypto::VaultKey::create(vaultPath, QString(), service));

        QByteArray key;
        QString err;
        QVERIFY(crypto::VaultKey::unlock(vaultPath, QString(), service, key, err));
        QCOMPARE(key.size(), 32);

        // sealing/opening of secrets
        const QByteArray sealed = crypto::VaultKey::sealSecret(key, "aad-1", "supersecret-cookie");
        QVERIFY(sealed.size() > 12 + 16);
        bool ok = false;
        QCOMPARE(crypto::VaultKey::openSecret(key, "aad-1", sealed, &ok),
                 QByteArrayLiteral("supersecret-cookie"));
        QVERIFY(ok);

        // An overlapping AAD must not decrypt
        bool ok2 = true;
        crypto::VaultKey::openSecret(key, "aad-2", sealed, &ok2);
        QVERIFY(!ok2);

        // Rekey into password mode
        QVERIFY(crypto::VaultKey::rekey(vaultPath, key, QStringLiteral("pass123"), service));
        QByteArray key2;
        QString err2;
        QVERIFY(!crypto::VaultKey::unlock(vaultPath, QString(), service, key2, err2));
        QCOMPARE(err2, QStringLiteral("password-required"));
        QVERIFY(crypto::VaultKey::unlock(vaultPath, QStringLiteral("pass123"), service, key2, err2));
        QCOMPARE(key2, key); // data key unchanged — secrets in the DB untouched
    }

    void storeCrud() {
        QTemporaryDir dir;
        store::Database db;
        QVERIFY(db.open(dir.path() + QStringLiteral("/test.db")));

        store::AccountStore st(db);
        model::Group g;
        g.name = QStringLiteral("Farm");
        QVERIFY(st.addGroup(g));
        QVERIFY(g.id > 0);

        model::Account a;
        a.username = QStringLiteral("tester");
        a.userId = 123;
        a.groupId = g.id;
        a.tags = {QStringLiteral("main")};
        a.fields.insert(QStringLiteral("Level"), 42);
        a.hasSession = true;
        QVERIFY(st.addAccount(a));
        QVERIFY(a.id > 0);

        const auto loaded = st.accountByUsername(QStringLiteral("tester"));
        QVERIFY(loaded.has_value());
        QCOMPARE(loaded->tags, QStringList{QStringLiteral("main")});
        QCOMPARE(loaded->fields.value(QStringLiteral("Level")).toInt(), 42);
        QCOMPARE(loaded->groupId, g.id);
        const auto byUserId = st.accountByUserId(123);
        QVERIFY(byUserId.has_value());
        QCOMPARE(byUserId->username, QStringLiteral("tester"));

        // Group rename and delete: the account is left without a group.
        g.name = QStringLiteral("Farm2");
        QVERIFY(st.updateGroup(g));
        QVERIFY(st.deleteGroup(g.id));
        const auto ungrouped = st.accountById(a.id);
        QVERIFY(ungrouped.has_value());
        QCOMPARE(ungrouped->groupId, qint64(0));
        a.groupId = 0; // the in-memory model drops the deleted group too

        // Vault
        store::Vault vault(db);
        vault.setDataKey(QByteArray(32, '\x07'));
        QVERIFY(vault.setCookie(a.id, QStringLiteral("COOKIEVALUE123")));
        QCOMPARE(vault.cookie(a.id), QStringLiteral("COOKIEVALUE123"));

        // A false cookie_expired is cleared by a successful launch.
        a.cookieExpired = true;
        QVERIFY(st.updateAccount(a));
        QVERIFY(st.markSessionValid(a.id));
        const auto valid = st.accountById(a.id);
        QVERIFY(valid.has_value());
        QVERIFY(!valid->cookieExpired);
        QVERIFY(valid->hasSession);

        // Settings guard
        store::Settings settings(db);
        settings.set(QStringLiteral("accounts.cookieValue"), QStringLiteral("hack"));
        QVERIFY(settings.get(QStringLiteral("accounts.cookieValue")).toString().isEmpty());
        settings.set(QStringLiteral("general.theme"), QStringLiteral("dark"));
        QCOMPARE(settings.get(QStringLiteral("general.theme")).toString(), QStringLiteral("dark"));

        // Deleting an account cascades to its secrets
        QVERIFY(st.deleteAccount(a.id));
        QVERIFY(vault.cookie(a.id).isEmpty());
    }

    void websocketFrames() {
        using ex::net::WebSocket;
        // Unmasked server text frame, short payload: 0x81 <len> data
        QByteArray f1;
        f1.append(static_cast<char>(0x81));
        f1.append(static_cast<char>(5));
        f1.append("hello");
        WebSocket::Frame out;
        int consumed = 0;
        QString err;
        QVERIFY(WebSocket::parseFrame(f1, out, consumed, err));
        QCOMPARE(out.opcode, quint8(0x1));
        QVERIFY(out.fin);
        QCOMPARE(out.payload, QByteArrayLiteral("hello"));
        QCOMPARE(consumed, 7);

        // 16-bit length (126)
        QByteArray longMsg(300, 'x');
        QByteArray f2;
        f2.append(static_cast<char>(0x81));
        f2.append(static_cast<char>(126));
        f2.append(static_cast<char>((300 >> 8) & 0xFF));
        f2.append(static_cast<char>(300 & 0xFF));
        f2.append(longMsg);
        QVERIFY(WebSocket::parseFrame(f2, out, consumed, err));
        QCOMPARE(out.payload.size(), 300);
        QCOMPARE(consumed, 304);

        // Not enough data — wait
        QVERIFY(!WebSocket::parseFrame(f2.left(10), out, consumed, err));
        QCOMPARE(err, QString());

        // 64-bit length (127)
        QByteArray f3;
        f3.append(static_cast<char>(0x81));
        f3.append(static_cast<char>(127));
        const quint64 len3 = 100;
        for (int i = 7; i >= 0; --i)
            f3.append(static_cast<char>((len3 >> (8 * i)) & 0xFF));
        f3.append(QByteArray(100, 'y'));
        QVERIFY(WebSocket::parseFrame(f3, out, consumed, err));
        QCOMPARE(out.payload.size(), 100);

        // Client (masked) frame
        QByteArray masked;
        masked.append(static_cast<char>(0x81));
        masked.append(static_cast<char>(0x80 | 4)); // mask + len
        const char mask[4] = {0x01, 0x02, 0x03, 0x04};
        masked.append(mask, 4);
        const char plain[4] = {'A', 'B', 'C', 'D'};
        for (int i = 0; i < 4; ++i)
            masked.append(static_cast<char>(plain[i] ^ mask[i]));
        QVERIFY(WebSocket::parseFrame(masked, out, consumed, err));
        QCOMPARE(out.payload, QByteArrayLiteral("ABCD"));
    }

    void httpCookieIsolation() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QList<QByteArray> cookieHeaders;
        QObject::connect(&server, &QTcpServer::newConnection, &server,
                         [&server, &cookieHeaders]() {
            while (QTcpSocket* socket = server.nextPendingConnection()) {
                auto buffer = std::make_shared<QByteArray>();
                QObject::connect(socket, &QTcpSocket::readyRead, socket,
                                 [socket, buffer, &cookieHeaders]() {
                    buffer->append(socket->readAll());
                    if (!buffer->contains("\r\n\r\n"))
                        return;
                    for (const QByteArray& line : buffer->split('\n')) {
                        if (line.toLower().startsWith("cookie:"))
                            cookieHeaders.append(line.trimmed());
                    }
                    socket->write("HTTP/1.1 200 OK\r\n"
                                  "Content-Length: 0\r\n"
                                  "Set-Cookie: .ROBLOSECURITY=FROM_SERVER; Path=/\r\n"
                                  "Connection: close\r\n\r\n");
                    socket->disconnectFromHost();
                });
                QObject::connect(socket, &QTcpSocket::disconnected, socket,
                                 &QObject::deleteLater);
            }
        });

        ex::roblox::Http http;
        const QUrl url(QStringLiteral("http://127.0.0.1:%1/")
                           .arg(server.serverPort()));
        const auto get = [&http, &url](const QByteArray& cookie) {
            QEventLoop loop;
            QNetworkRequest req(url);
            req.setRawHeader(QByteArrayLiteral("Cookie"),
                             QByteArrayLiteral(".ROBLOSECURITY=") + cookie);
            http.get(req, [&loop](const ex::roblox::HttpResponse&) {
                loop.quit();
            });
            QTimer::singleShot(5000, &loop, &QEventLoop::quit);
            loop.exec();
        };
        get("FIRST");
        get("SECOND");

        QTRY_COMPARE(cookieHeaders.size(), 2);
        QVERIFY(cookieHeaders.at(0).contains("FIRST"));
        QVERIFY(cookieHeaders.at(1).contains("SECOND"));
        // jar must not mix in a cookie from the previous account/response.
        QVERIFY(!cookieHeaders.at(1).contains("FIRST"));
        QVERIFY(!cookieHeaders.at(1).contains("FROM_SERVER"));
    }

    void robloxInstallResolve() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir root(dir.path());
        QVERIFY(root.mkpath(QStringLiteral("version-old")));
        QVERIFY(root.mkpath(QStringLiteral("version-new")));

        const QString oldExe =
            root.filePath(QStringLiteral("version-old/RobloxPlayerBeta.exe"));
        const QString newExe =
            root.filePath(QStringLiteral("version-new/RobloxPlayerBeta.exe"));
        for (const QString& path : {oldExe, newExe}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("stub");
            file.close();
        }
        QFile oldFile(oldExe);
        QVERIFY(oldFile.open(QIODevice::ReadWrite));
        QVERIFY(oldFile.setFileTime(QDateTime::currentDateTime().addDays(-2),
                                    QFileDevice::FileModificationTime));
        oldFile.close();
        QFile newFile(newExe);
        QVERIFY(newFile.open(QIODevice::ReadWrite));
        QVERIFY(newFile.setFileTime(QDateTime::currentDateTime(),
                                    QFileDevice::FileModificationTime));
        newFile.close();

        const QString expectedNew = QFileInfo(newExe).absoluteFilePath();
        const QString expectedOld = QFileInfo(oldExe).absoluteFilePath();
        QCOMPARE(platform::findRobloxPlayerInFolder(dir.path()), expectedNew);
        QCOMPARE(platform::findRobloxPlayerInFolder(
                     root.filePath(QStringLiteral("version-old"))),
                 expectedOld);
        QCOMPARE(platform::resolveRobloxPlayerPath(
                     root.filePath(QStringLiteral("version-old"))),
                 expectedOld);
        QCOMPARE(platform::resolveRobloxPlayerPath(
                     QStringLiteral("\"%1\"").arg(newExe)),
                 expectedNew);
        QVERIFY(platform::resolveRobloxPlayerPath(
                    root.filePath(QStringLiteral("missing")))
                    .isEmpty());
        QVERIFY(platform::resolveRobloxPlayerPath(
                    QStringLiteral("C:/Windows/System32/notepad.exe"))
                    .isEmpty());
    }

    void analyticsLedger() {
        QTemporaryDir dir;
        store::Database db;
        QVERIFY(db.open(dir.path() + QStringLiteral("/ledger.db")));
        store::AccountStore st(db);

        model::Account a;
        a.username = QStringLiteral("ledger");
        QVERIFY(st.addAccount(a));

        const auto insertSession = [&](const QString& start, const QString& end) {
            store::Stmt s(db.handle(), QStringLiteral(
                "INSERT INTO session_ledger (account_id, started_at, ended_at) VALUES (?,?,?)"));
            s.bind(1, a.id);
            s.bind(2, start);
            if (end.isEmpty())
                s.bindNull(3);
            else
                s.bind(3, end);
            QVERIFY(s.exec());
        };
        insertSession(QStringLiteral("2026-10-25T10:00:00Z"),
                      QStringLiteral("2026-10-25T10:01:40Z")); // 100 c
        insertSession(QStringLiteral("2026-10-25T09:59:00Z"),
                      QStringLiteral("2026-10-25T10:01:00Z")); // 60 s after the clipboard read
        insertSession(QStringLiteral("2026-10-25T10:02:00Z"), QString()); // open

        const QDateTime from = QDateTime::fromString(
            QStringLiteral("2026-10-25T10:00:00Z"), Qt::ISODate);
        const QDateTime to = QDateTime::fromString(
            QStringLiteral("2026-10-25T10:03:00Z"), Qt::ISODate);
        QVERIFY(from.isValid());
        QVERIFY(to.isValid());

        // 100 + 60 (clipped from) + 60 (open one clipped to) = 220.
        QCOMPARE(st.sessionSecondsFor(a.id, from, to), qint64(220));
        QCOMPARE(st.sessionCountFor(a.id, from, to), 3);

        // Orphaned sessions close with zero duration.
        QVERIFY(st.closeOpenSessions(QStringLiteral("test")));
        QCOMPARE(st.sessionSecondsFor(a.id, from, to), qint64(160));
    }

    void jobObjectLimits() {
        QProcess child;
        child.start(QStringLiteral("C:/Windows/System32/ping.exe"),
                    {QStringLiteral("-n"), QStringLiteral("30"),
                     QStringLiteral("127.0.0.1")});
        QVERIFY(child.waitForStarted(3000));
        const qint64 pid = static_cast<qint64>(child.processId());
        QVERIFY(pid > 0);

        // The RAM limit (working set) is Job-Object independent and should work
        // even where the terminal sandbox forbids job assignment.
        const auto report = platform::RamLimiter::applyLimits(pid, 25, 2048);
        QVERIFY(report.memory);
        QVERIFY(report.cpuHard || report.cpuSoft);
        QVERIFY(platform::RamLimiter::clearLimits(pid));
        QVERIFY(platform::RamLimiter::clearLimits(pid)); // clearing it again is safe

        child.kill();
        QVERIFY(child.waitForFinished(3000));
    }

    void cdpBrowserDetect() {
        // findBrowser on Windows almost always returns Edge
        const QString b = ex::roblox::CdpLoginService::findBrowser();
        if (!b.isEmpty())
            QVERIFY(b.endsWith(QLatin1String("msedge.exe")) || b.endsWith(QLatin1String("chrome.exe")));
        // candidates is a non-empty list of standard paths
        QVERIFY(!ex::roblox::CdpLoginService::browserCandidates().isEmpty());
    }

    void multiRobloxToggle() {
        launcher::MultiRoblox multi;
        QVERIFY(!multi.enabled());
        QVERIFY(!multi.active());
        QCOMPARE(multi.state(), launcher::MultiRoblox::State::Off);

        // Holding when the mutex is free, Waiting when a Roblox client owns it.
        // Both are valid here; only Error means the port is broken.
        const auto on = multi.setEnabled(true);
        QVERIFY(on == launcher::MultiRoblox::State::Holding
                || on == launcher::MultiRoblox::State::Waiting);
        QVERIFY(multi.enabled());
        QCOMPARE(multi.state(), on);

        QVERIFY(multi.setEnabled(false) == launcher::MultiRoblox::State::Off);
        QVERIFY(!multi.enabled());
        // Releasing twice must not touch a mutex we no longer own.
        multi.release();
        QCOMPARE(multi.state(), launcher::MultiRoblox::State::Off);
    }
};

QTEST_MAIN(TestCore)
#include "test_core.moc"

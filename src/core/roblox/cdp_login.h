#pragma once

#include "core/net/websocket.h"

#include <QByteArray>

#include <QNetworkAccessManager>
#include <QProcess>
#include <QRect>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>

class QJsonObject;

namespace ex::roblox {

struct CdpLoginRequest {
    QString username;
    QString password;
};

// One login session: browser (Edge/Chrome) + roblox.com/login tab +
// autofill via CDP Runtime.evaluate + capture of the HttpOnly cookie
// Reads .ROBLOSECURITY via CDP Network.getCookies (Astro EdgeCDP approach).
//
// When the app runs as administrator, the browser is started
// de-elevated (explorer token): Chrome 136+ refuses remote
// debugging in an elevated context and exits immediately.
//
// Secrets never reach the browser's argv: login/password are passed
// only inside the CDP call body over the localhost WebSocket.
class CdpLoginSession : public QObject {
    Q_OBJECT
public:
    enum class Status {
        StartingBrowser = 0, // waiting for DevToolsActivePort
        Connecting,          // WebSocket to the tab
        FillingForm,         // fill in the form
        WaitingForUser,      // waiting for the login (captcha/2FA/button)
        CookieCaptured,
        Failed,
        Cancelled,
    };
    Q_ENUM(Status)

    explicit CdpLoginSession(QObject* parent = nullptr);
    ~CdpLoginSession() override;

    // browserPath is the found msedge/chrome. windowRect is the window position
    // (for the bulk grid); an empty QRect = best effort.
    void start(const QString& browserPath, const CdpLoginRequest& req, const QRect& windowRect);

    void cancel();
    Status status() const { return m_status; }
    QString username() const { return m_request.username; }

signals:
    void statusChanged(ex::roblox::CdpLoginSession::Status status, const QString& detail);
    // cookie captured; userId/Name/DisplayName obtained
    // fetch('/my/account/json') FROM THE PAGE ITSELF (C# RAM pattern,
    // AccountBrowser.cs:329). username is the requested login (for the table),
    // realUsername/realDisplayName are the real Roblox Name/DisplayName.
    // The password is stored in the vault for re-login.
    void captured(const QString& username, const QString& cookie, const QString& password,
                  qint64 userId, const QString& realUsername, const QString& realDisplayName);
    void finished();
    void failedInternal(const QString& detail); // the service recomputes statistics

private:
    void setStatus(Status s, const QString& detail);
    void startBrowser(const QStringList& args);
    QStringList baseArgs() const;
    QStringList minimalArgs() const;
    bool browserAlive(unsigned long* exitCode = nullptr);
    void stopBrowser();
    void pollDevToolsPort();
    void fetchTargetList();
    void connectToTarget(const QString& wsUrl);
    void sendCommand(const QString& method, const QJsonObject& params,
                     std::function<void(const QJsonObject& result)> cb);
    void tryFillForm();
    void pollCookies();
    void fetchAccountJsonAndCapture();
    void captureCookieFromJar(qint64 userId, const QString& realUsername,
                              const QString& realDisplayName);
    void finishWithCookie(const QString& cookie, qint64 userId, const QString& realUsername,
                          const QString& realDisplayName);

    QString m_browserPath;
    CdpLoginRequest m_request;
    QRect m_windowRect;
    QString m_userDataDir;

    QProcess m_browser;                        // normal (non-elevated) launch
    void* m_deelevatedHandle = nullptr;        // elevated: HANDLE from launchDeelevated
    bool m_retriedMinimal = false;
    unsigned long m_lastExitCode = 0;

    QNetworkAccessManager m_nam;
    ex::net::WebSocket m_ws;

    Status m_status = Status::StartingBrowser;
    QTimer m_pollTimer;       // DevToolsActivePort / cookies / form retry
    int m_port = 0;
    int m_nextCommandId = 1;
    QHash<int, std::function<void(const QJsonObject&)>> m_callbacks;
    int m_fillAttempts = 0;
    int m_totalMs = 0;
    bool m_done = false;

    // Capture mechanics from C# RAM (Page_RequestFinished): the cookie comes
    // ONLY after a successful (200) POST to auth.roblox.com/v2/login,
    // /v2/signup or /users/<id>/two-step-verification/login — then
    // cookie jar is guaranteed to be an authorized session, not a guest one.
    QSet<QString> m_loginRequestIds;  // requestId of the login POSTs
    QSet<QString> m_okLoginIds;       // of those, the ones that returned 200
    bool m_sawLoginFinish = false;    // the login finished successfully
    bool m_captureInFlight = false;   // fetch/redeem is already running
    int m_captureAttempts = 0;
};

// Manager: browser detection, window grid, parallel sessions (2 at a time, like RAM).
class CdpLoginService : public QObject {
    Q_OBJECT
public:
    // Path to Edge/Chrome or empty (Edge first — it is always installed).
    static QString findBrowser();
    static QStringList browserCandidates();

    explicit CdpLoginService(QObject* parent = nullptr);

    void login(const CdpLoginRequest& req);
    void loginBulk(const QList<CdpLoginRequest>& requests, int concurrency = 2);
    void cancelAll();
    int activeCount() const;

signals:
    void sessionStatus(const QString& username, int status, const QString& detail);
    void loginSucceeded(const QString& username, const QString& cookie, const QString& password,
                        qint64 userId, const QString& realUsername, const QString& realDisplayName);
    void loginFailed(const QString& username, const QString& error);
    void allFinished();

private:
    void pumpQueue();

    QString m_browserPath;
    QList<CdpLoginRequest> m_queue;
    QList<CdpLoginSession*> m_active;
    int m_concurrency = 2;
    int m_succeeded = 0;
    int m_failed = 0;
    int m_total = 0;
};

} // namespace ex::roblox

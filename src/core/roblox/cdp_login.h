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

// Одна сессия входа: браузер (Edge/Chrome) + вкладка roblox.com/login +
// автозаполнение через CDP Runtime.evaluate + перехват HttpOnly cookie
// .ROBLOSECURITY через CDP Network.getCookies (подход Astro EdgeCDP).
//
// Если приложение запущено с правами администратора, браузер запускается
// с понижением прав (токен explorer): Chrome 136+ отказывается от remote
// debugging в повышенном контексте и мгновенно выходит.
//
// Секреты не попадают в argv процесса браузера: логин/пароль передаются
// только в теле CDP-вызова по localhost WebSocket.
class CdpLoginSession : public QObject {
    Q_OBJECT
public:
    enum class Status {
        StartingBrowser = 0, // ждём DevToolsActivePort
        Connecting,          // WebSocket к вкладке
        FillingForm,         // автозаполнение формы
        WaitingForUser,      // ждём входа (капча/2FA/кнопка)
        CookieCaptured,
        Failed,
        Cancelled,
    };
    Q_ENUM(Status)

    explicit CdpLoginSession(QObject* parent = nullptr);
    ~CdpLoginSession() override;

    // browserPath — найденный msedge/chrome. windowRect — позиция окна
    // (для bulk-сетки); пустой QRect = как получится.
    void start(const QString& browserPath, const CdpLoginRequest& req, const QRect& windowRect);

    void cancel();
    Status status() const { return m_status; }
    QString username() const { return m_request.username; }

signals:
    void statusChanged(ex::roblox::CdpLoginSession::Status status, const QString& detail);
    // cookie перехвачен; userId/Name/DisplayName получены
    // fetch('/my/account/json') ИЗ САМОЙ СТРАНИЦЫ (паттерн C# RAM
    // AccountBrowser.cs:329). username — запрошенный логин (для таблицы),
    // realUsername/realDisplayName — настоящие Name/DisplayName Roblox.
    // Пароль — чтобы сохранить в vault для ре-логина.
    void captured(const QString& username, const QString& cookie, const QString& password,
                  qint64 userId, const QString& realUsername, const QString& realDisplayName);
    void finished();
    void failedInternal(const QString& detail); // сервис пересчитывает статистику

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

    QProcess m_browser;                        // обычный (не elevated) запуск
    void* m_deelevatedHandle = nullptr;        // elevated: HANDLE из launchDeelevated
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

    // Механика перехвата из C# RAM (Page_RequestFinished): cookie берём
    // ТОЛЬКО после успешного (200) POST на auth.roblox.com/v2/login,
    // /v2/signup или /users/<id>/two-step-verification/login — тогда в
    // cookie jar гарантированно авторизованная сессия, а не гостевая.
    QSet<QString> m_loginRequestIds;  // requestId POST-запросов логина
    QSet<QString> m_okLoginIds;       // из них получившие 200
    bool m_sawLoginFinish = false;    // логин завершился успешно
    bool m_captureInFlight = false;   // fetch/redeem уже выполняется
    int m_captureAttempts = 0;
};

// Менеджер: детект браузера, сетка окон, параллельные сессии (по 2, как RAM).
class CdpLoginService : public QObject {
    Q_OBJECT
public:
    // Путь к Edge/Chrome или пустая строка (Edge приоритетнее — есть везде).
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

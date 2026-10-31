#include "core/roblox/cdp_login.h"

#include "core/util/log.h"
#include "core/util/paths.h"
#include "core/util/redact.h"
#include "platform/windows/elevation.h"
#include "platform/windows/window_mgr.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUuid>
#include <QUrlQuery>

#include <utility>

namespace ex::roblox {

using Status = CdpLoginSession::Status;

namespace {
constexpr int kDevToolsTimeoutMs = 20000;  // waiting for the port after the browser starts
constexpr int kFillMaxAttempts = 20;       // 30 s for React hydration
constexpr int kLoginTimeoutMs = 10 * 60 * 1000; // 10 minutes for captcha/2FA
constexpr int kPollIntervalMs = 1500;

// Autofill JS: React-controlled inputs need the native setter
// + an input event (otherwise the value never reaches state).
QString buildFillScript(const QString& username, const QString& password)
{
    // Values as a JSON array (quotes and slashes escaped properly)
    const QByteArray creds = QJsonDocument(QJsonArray{username, password}).toJson(QJsonDocument::Compact);
    return QStringLiteral(R"(
(function(){
  try {
    const creds = %1;
    const u = creds[0], p = creds[1];
    const findInput = (sels) => {
      for (const s of sels) {
        const el = document.querySelector(s);
        if (el) return el;
      }
      return null;
    };
    const userEl = findInput(['#login-username', 'input[name="username"]', 'input[autocomplete="username"]']);
    const passEl = findInput(['#login-password', 'input[name="password"]', 'input[type="password"]']);
    if (!userEl || !passEl) return 'form-not-found';
    const setNative = (el, value) => {
      const proto = el instanceof HTMLTextAreaElement ? HTMLTextAreaElement.prototype : HTMLInputElement.prototype;
      const setter = Object.getOwnPropertyDescriptor(proto, 'value').set;
      setter.call(el, value);
      el.dispatchEvent(new Event('input', { bubbles: true }));
      el.dispatchEvent(new Event('change', { bubbles: true }));
    };
    setNative(userEl, u);
    setNative(passEl, p);
    const btn = document.querySelector('#login-button, button[type="submit"], button[data-testid="login-button"]');
    return btn ? 'filled-button' : 'filled';
  } catch (e) { return 'js-error:' + e.message; }
})()
)")
        .arg(QString::fromUtf8(creds));
}

QString buildClickScript()
{
    return QStringLiteral(R"(
(function(){
  const btn = document.querySelector('#login-button, button[type="submit"], button[data-testid="login-button"]');
  if (btn) { btn.click(); return 'clicked'; }
  return 'no-button';
})()
)");
}
} // namespace

// ------------------------------------------------------------------ session

CdpLoginSession::CdpLoginSession(QObject* parent)
    : QObject(parent)
{
    connect(&m_ws, &ex::net::WebSocket::textMessageReceived, this, [this](const QString& msg) {
        const QJsonObject o = QJsonDocument::fromJson(msg.toUtf8()).object();
        const int id = o[QStringLiteral("id")].toInt(-1);
        if (id < 0) {
            // Network.* events — capture mechanics from C# RAM:
            // wait for a successful POST to auth.roblox.com/v2/login (or 2FA/signup),
            // Take the cookie ONLY after it (otherwise a guest session is captured).
            const QString method = o[QStringLiteral("method")].toString();
            const QJsonObject params = o[QStringLiteral("params")].toObject();
            if (method == QLatin1String("Network.requestWillBeSent")) {
                const QJsonObject request = params[QStringLiteral("request")].toObject();
                const QString url = request[QStringLiteral("url")].toString();
                if (request[QStringLiteral("method")].toString() == QLatin1String("POST")
                    && url.startsWith(QLatin1String("https://auth.roblox.com/"))
                    && (url.contains(QLatin1String("/v2/login"))
                        || url.contains(QLatin1String("/v2/signup"))
                        || url.contains(QLatin1String("two-step-verification/login")))) {
                    m_loginRequestIds.insert(params[QStringLiteral("requestId")].toString());
                }
            } else if (method == QLatin1String("Network.responseReceived")) {
                const QString requestId = params[QStringLiteral("requestId")].toString();
                if (m_loginRequestIds.contains(requestId)) {
                    const int status = params[QStringLiteral("response")].toObject()
                                           [QStringLiteral("status")].toInt();
                    if (status == 200)
                        m_okLoginIds.insert(requestId);
                    else
                        EXLOG_INFO("cdp") << "login attempt response" << status;
                }
            } else if (method == QLatin1String("Network.loadingFinished")) {
                const QString requestId = params[QStringLiteral("requestId")].toString();
                if (m_okLoginIds.contains(requestId) && !m_sawLoginFinish) {
                    m_sawLoginFinish = true;
                    EXLOG_INFO("cdp") << "successful login request finished — capturing cookie";
                    // Half a second for Set-Cookie to apply and the page to navigate
                    QTimer::singleShot(500, this, [this]() {
                        if (!m_done && m_sawLoginFinish)
                            fetchAccountJsonAndCapture();
                    });
                }
            }
            return;
        }
        const auto it = m_callbacks.find(id);
        if (it == m_callbacks.end())
            return;
        const auto cb = it.value();
        m_callbacks.erase(it);
        if (o.contains(QStringLiteral("error"))) {
            EXLOG_DEBUG("cdp") << "command error:" << o[QStringLiteral("error")].toObject()[QStringLiteral("message")].toString();
            cb({});
        } else {
            cb(o[QStringLiteral("result")].toObject());
        }
    });
    connect(&m_ws, &ex::net::WebSocket::errorOccurred, this, [this](const QString& err) {
        if (!m_done)
            setStatus(Status::Failed, err);
    });
    connect(&m_ws, &ex::net::WebSocket::disconnected, this, [this]() {
        if (!m_done && m_status == Status::WaitingForUser)
            setStatus(Status::Failed, QStringLiteral("browser-window-closed"));
    });
}

CdpLoginSession::~CdpLoginSession()
{
    cancel();
}

QStringList CdpLoginSession::baseArgs() const
{
    QStringList args{
        QStringLiteral("--user-data-dir=%1").arg(m_userDataDir),
        QStringLiteral("--remote-debugging-port=0"), // auto port -> DevToolsActivePort
        QStringLiteral("--no-first-run"),
        QStringLiteral("--no-default-browser-check"),
        QStringLiteral("--disable-sync"),
    };
    if (m_windowRect.isValid()) {
        args.append(QStringLiteral("--window-size=%1,%2")
                        .arg(m_windowRect.width()).arg(m_windowRect.height()));
        args.append(QStringLiteral("--window-position=%1,%2")
                        .arg(m_windowRect.x()).arg(m_windowRect.y()));
    }
    args.append(QStringLiteral("https://www.roblox.com/login"));
    return args;
}

QStringList CdpLoginSession::minimalArgs() const
{
    // Retry after an instant exit: minimal flags only.
    return {
        QStringLiteral("--user-data-dir=%1").arg(m_userDataDir),
        QStringLiteral("--remote-debugging-port=0"),
        QStringLiteral("https://www.roblox.com/login"),
    };
}

void CdpLoginSession::startBrowser(const QStringList& args)
{
    if (ex::platform::isElevated()) {
        // Chrome 136+ refuses remote debugging in an elevated
        // context — so the browser starts from the explorer token (RAM approach).
        m_deelevatedHandle = ex::platform::launchDeelevated(m_browserPath, args, m_userDataDir);
        if (!m_deelevatedHandle) {
            EXLOG_WARN("cdp") << "deelevated launch failed, falling back to QProcess";
            m_browser.setProgram(m_browserPath);
            m_browser.setArguments(args);
            m_browser.start();
            return;
        }
        return;
    }
    m_browser.setProcessChannelMode(QProcess::MergedChannels);
    m_browser.setProgram(m_browserPath);
    m_browser.setArguments(args);
    m_browser.start();
}

bool CdpLoginSession::browserAlive(unsigned long* exitCode)
{
    if (m_deelevatedHandle)
        return !ex::platform::processHasExited(m_deelevatedHandle, exitCode);
    if (exitCode)
        *exitCode = static_cast<unsigned long>(m_browser.exitCode());
    return m_browser.state() == QProcess::Running;
}

void CdpLoginSession::stopBrowser()
{
    if (m_deelevatedHandle) {
        ex::platform::terminateProcessByHandle(m_deelevatedHandle);
        ex::platform::closeProcessHandle(m_deelevatedHandle);
        m_deelevatedHandle = nullptr;
        return;
    }
    if (m_browser.state() != QProcess::NotRunning) {
        disconnect(&m_browser, nullptr, nullptr, nullptr);
        m_browser.kill();
        m_browser.waitForFinished(2000);
    }
}

void CdpLoginSession::start(const QString& browserPath, const CdpLoginRequest& req,
                            const QRect& windowRect)
{
    m_browserPath = browserPath;
    m_request = req;
    m_windowRect = windowRect;
    m_done = false;
    setStatus(Status::StartingBrowser, QString());

    // Temporary profile: every session is isolated (RAM approach)
    m_userDataDir = ex::util::AppPaths::cacheDir()
        + QStringLiteral("/cdp/%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QDir().mkpath(m_userDataDir);

    connect(&m_browser, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (!m_done && e == QProcess::FailedToStart)
            setStatus(Status::Failed, QStringLiteral("browser-start-failed (не удалось запустить браузер)"));
    });
    connect(&m_browser, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        if (!m_done && m_status == Status::StartingBrowser) {
            // pollDevToolsPort sorts it out (retry / final error)
            m_lastExitCode = static_cast<unsigned long>(code);
            return;
        }
        if (!m_done)
            setStatus(Status::Failed, QStringLiteral("browser-exited (code %1)").arg(code));
    });

    startBrowser(baseArgs());

    // Wait for the DevToolsActivePort file
    m_pollTimer.setInterval(kPollIntervalMs / 3);
    m_totalMs = 0;
    connect(&m_pollTimer, &QTimer::timeout, this, &CdpLoginSession::pollDevToolsPort);
    m_pollTimer.start();
}

void CdpLoginSession::setStatus(Status s, const QString& detail)
{
    if (m_done)
        return; // terminal state is one-way
    m_status = s;
    EXLOG_INFO("cdp") << "session" << m_request.username << "status" << static_cast<int>(s) << detail;
    emit statusChanged(s, detail);

    const bool terminal = (s == Status::CookieCaptured || s == Status::Failed || s == Status::Cancelled);
    if (!terminal)
        return;

    m_done = true;
    m_pollTimer.stop();
    m_callbacks.clear();
    m_ws.close();
    stopBrowser();
    // Delete the temporary profile (2 tries — files are not freed instantly)
    if (!m_userDataDir.isEmpty()) {
        if (!QDir(m_userDataDir).removeRecursively())
            QTimer::singleShot(2000, this, [dir = m_userDataDir]() { QDir(dir).removeRecursively(); });
    }
    if (s == Status::Failed)
        emit failedInternal(detail);
    emit finished();
}

void CdpLoginSession::pollDevToolsPort()
{
    m_totalMs += m_pollTimer.interval();

    // Did the browser die before DevToolsActivePort appear?
    unsigned long exitCode = 0;
    if (!browserAlive(&exitCode)) {
        // Retry with minimal flags (guards against moody browser versions)
        if (!m_retriedMinimal) {
            m_retriedMinimal = true;
            m_lastExitCode = exitCode;
            EXLOG_WARN("cdp") << "browser exited (code" << exitCode
                              << ") before DevTools; retrying with minimal flags";
            m_totalMs = 0;
            startBrowser(minimalArgs());
            return;
        }
        setStatus(Status::Failed,
                  QStringLiteral("browser-exited (code %1) — браузер закрылся до включения отладки")
                      .arg(exitCode ? exitCode : m_lastExitCode));
        return;
    }
    if (m_totalMs > kDevToolsTimeoutMs) {
        setStatus(Status::Failed,
                  QStringLiteral("devtools-port-timeout — порт отладки не появился за %1 c")
                      .arg(kDevToolsTimeoutMs / 1000));
        return;
    }
    const QString portFile = m_userDataDir + QStringLiteral("/DevToolsActivePort");
    QFile f(portFile);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QString firstLine = QString::fromUtf8(f.readLine()).trimmed();
    bool ok = false;
    m_port = firstLine.toInt(&ok);
    if (!ok || m_port <= 0)
        return;

    m_pollTimer.stop();
    setStatus(Status::Connecting, QString());
    fetchTargetList();
}

void CdpLoginSession::fetchTargetList()
{
    QNetworkRequest req{QUrl(QStringLiteral("http://127.0.0.1:%1/json/list").arg(m_port))};
    QNetworkReply* reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (m_done)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            setStatus(Status::Failed, QStringLiteral("targets-fetch-failed"));
            return;
        }
        const QJsonArray targets = QJsonDocument::fromJson(reply->readAll()).array();
        for (const auto& v : targets) {
            const QJsonObject t = v.toObject();
            const QString type = t[QStringLiteral("type")].toString();
            const QString url = t[QStringLiteral("url")].toString();
            const QString wsUrl = t[QStringLiteral("webSocketDebuggerUrl")].toString();
            if (type == QLatin1String("page") && url.contains(QLatin1String("roblox.com")) && !wsUrl.isEmpty()) {
                connectToTarget(wsUrl);
                return;
            }
        }
        // The target tab does not exist yet — retry in a second
        QTimer::singleShot(1000, this, &CdpLoginSession::fetchTargetList);
    });
}

void CdpLoginSession::connectToTarget(const QString& wsUrl)
{
    connect(&m_ws, &ex::net::WebSocket::connected, this, &CdpLoginSession::tryFillForm,
            Qt::UniqueConnection);
    m_ws.connectToUrl(QUrl(wsUrl));
}

void CdpLoginSession::sendCommand(const QString& method, const QJsonObject& params,
                                  std::function<void(const QJsonObject&)> cb)
{
    if (!m_ws.isOpen()) {
        if (cb)
            cb({});
        return;
    }
    QJsonObject cmd;
    cmd[QStringLiteral("id")] = m_nextCommandId++;
    cmd[QStringLiteral("method")] = method;
    if (!params.isEmpty())
        cmd[QStringLiteral("params")] = params;
    m_callbacks.insert(cmd[QStringLiteral("id")].toInt(), std::move(cb));
    m_ws.sendTextMessage(QString::fromUtf8(QJsonDocument(cmd).toJson(QJsonDocument::Compact)));
}

void CdpLoginSession::tryFillForm()
{
    if (m_done)
        return;
    setStatus(Status::FillingForm, QString());

    // Enable the Network domain (needed for Network.getCookies)
    sendCommand(QStringLiteral("Network.enable"), {}, [this](const QJsonObject&) {
        // Start polling for the cookie (also finds an existing session)
        m_pollTimer.stop();
        m_pollTimer.disconnect(this);
        connect(&m_pollTimer, &QTimer::timeout, this, &CdpLoginSession::pollCookies);
        m_pollTimer.setInterval(kPollIntervalMs);
        m_pollTimer.start();
        pollCookies();
    });

    // Repeat form autofill (React may not be hydrated yet)
    sendCommand(QStringLiteral("Runtime.evaluate"),
                {{QStringLiteral("expression"), buildFillScript(m_request.username, m_request.password)},
                 {QStringLiteral("returnByValue"), true}},
                [this](const QJsonObject& result) {
        const QString value = result[QStringLiteral("result")].toObject()
                                  [QStringLiteral("value")].toString();
        if (value == QLatin1String("filled-button")) {
            // Form is filled — press the login button
            sendCommand(QStringLiteral("Runtime.evaluate"),
                        {{QStringLiteral("expression"), buildClickScript()},
                         {QStringLiteral("returnByValue"), true}},
                        [this](const QJsonObject&) {
                setStatus(Status::WaitingForUser, QStringLiteral("waiting"));
            });
        } else if (value == QLatin1String("form-not-found") && m_fillAttempts < kFillMaxAttempts) {
            ++m_fillAttempts;
            QTimer::singleShot(1500, this, &CdpLoginSession::tryFillForm);
        } else if (value.startsWith(QLatin1String("js-error"))) {
            setStatus(Status::Failed, value);
        } else {
            // filled button — the user presses play themselves
            setStatus(Status::WaitingForUser, QStringLiteral("manual-click"));
        }
    });
}

void CdpLoginSession::pollCookies()
{
    if (m_done)
        return;
    m_totalMs += kPollIntervalMs;
    if (m_totalMs > kLoginTimeoutMs) {
        setStatus(Status::Failed,
                  QStringLiteral("login-timeout — вход не был завершён за %1 мин")
                      .arg(kLoginTimeoutMs / 60000));
        return;
    }
    // Before a successful login POST the jar cookie may be a guest one — skip it
    if (!m_sawLoginFinish)
        return;

    fetchAccountJsonAndCapture();
}

void CdpLoginSession::fetchAccountJsonAndCapture()
{
    if (m_done || !m_sawLoginFinish || m_captureInFlight)
        return;
    m_captureInFlight = true;

    // C# RAM pattern (AccountBrowser.cs:329): account data comes
    // fetch('/my/account/json') FROM THE PAGE ITSELF — the browser session
    // is authorized, so the answer is trustworthy.
    sendCommand(QStringLiteral("Runtime.evaluate"),
                {{QStringLiteral("expression"),
                  QStringLiteral(
                      "fetch('/my/account/json', {credentials:'include'})"
                      ".then(r => r.ok ? r.text() : Promise.reject('http ' + r.status))")},
                 {QStringLiteral("awaitPromise"), true},
                 {QStringLiteral("returnByValue"), true}},
                [this](const QJsonObject& result) {
        if (m_done || !m_sawLoginFinish) {
            m_captureInFlight = false;
            return;
        }
        // an exception inside the page -> result.exceptionDetails
        if (result.contains(QStringLiteral("exceptionDetails"))) {
            EXLOG_INFO("cdp") << "account fetch not ready yet (login pending)";
            m_captureInFlight = false;
            return; // the next timer tick retries
        }
        const QJsonObject value = result[QStringLiteral("result")].toObject();
        if (value[QStringLiteral("type")].toString() != QLatin1String("string")) {
            m_captureInFlight = false;
            return;
        }
        const QJsonObject info = QJsonDocument::fromJson(
            value[QStringLiteral("value")].toString().toUtf8()).object();
        const qint64 userId = info[QStringLiteral("UserId")].toInteger();
        if (userId <= 0) {
            EXLOG_INFO("cdp") << "account json has no UserId — login pending";
            m_captureInFlight = false;
            return;
        }
        const QString name = info[QStringLiteral("Name")].toString();
        const QString display = info[QStringLiteral("DisplayName")].toString();
        EXLOG_INFO("cdp") << "account json ok: user id" << userId;

        // RAM/CEF approach: take the cookie from the browser jar and use it
        // from outside. An in-page ticket script hits CORS in Chrome/Edge and
        // is not needed — our HTTP client gets the ticket (RAM sequence).
        captureCookieFromJar(userId, name.isEmpty() ? display : name,
                             display.isEmpty() ? name : display);
    });
}

void CdpLoginSession::captureCookieFromJar(qint64 userId, const QString& realUsername,
                                           const QString& realDisplayName)
{
    sendCommand(QStringLiteral("Network.getCookies"),
                {{QStringLiteral("urls"), QJsonArray{QStringLiteral("https://www.roblox.com"),
                                                     QStringLiteral("https://roblox.com")}}},
                [this, userId, realUsername, realDisplayName](const QJsonObject& cres) {
        if (m_done)
            return;
        const QJsonArray cookies = cres[QStringLiteral("cookies")].toArray();
        QString best;
        for (const auto& v : cookies) {
            const QJsonObject c = v.toObject();
            if (c[QStringLiteral("name")].toString() != QLatin1String(".ROBLOSECURITY"))
                continue;
            const QString raw = c[QStringLiteral("value")].toString();
            EXLOG_INFO("cdp") << "jar .ROBLOSECURITY: domain"
                              << c[QStringLiteral("domain")].toString()
                              << "path" << c[QStringLiteral("path")].toString()
                              << "len" << raw.size();
            // If the jar holds several cookies with the same name (host-only and .roblox.com),
            // the longest value wins — that is the authorized session.
            if (raw.size() > best.size())
                best = raw;
        }
        if (!best.isEmpty()) {
            const QString cookie = ex::util::redact::normalizeCookieValue(best);
            if (!cookie.isEmpty()) {
                finishWithCookie(cookie, userId, realUsername, realDisplayName);
                return;
            }
        }
        m_captureInFlight = false;
        if (++m_captureAttempts >= 10) {
            EXLOG_WARN("cdp") << ".ROBLOSECURITY not found in cookie jar";
            setStatus(Status::Failed, QStringLiteral("cookie-not-found"));
        }
    });
}

void CdpLoginSession::finishWithCookie(const QString& cookie, qint64 userId,
                                       const QString& realUsername,
                                       const QString& realDisplayName)
{
    if (m_done)
        return;
    m_captureInFlight = false;
    EXLOG_INFO("cdp") << "cookie captured:" << cookie.size()
                      << "chars, warning-prefix:"
                      << cookie.contains(QLatin1String("_|WARNING:"));
    emit captured(m_request.username, cookie, m_request.password, userId,
                  realUsername, realDisplayName);
    setStatus(Status::CookieCaptured, QString());
}

void CdpLoginSession::cancel()
{
    if (m_done)
        return;
    setStatus(Status::Cancelled, QString());
}

// ------------------------------------------------------------------ service

QStringList CdpLoginService::browserCandidates()
{
    const QString pf86 = QStringLiteral("C:/Program Files (x86)");
    const QString pf = QStringLiteral("C:/Program Files");
    const QString lad = QString::fromWCharArray(_wgetenv(L"LOCALAPPDATA"));
    return {
        pf + QStringLiteral("/Microsoft/Edge/Application/msedge.exe"),
        pf86 + QStringLiteral("/Microsoft/Edge/Application/msedge.exe"),
        pf + QStringLiteral("/Google/Chrome/Application/chrome.exe"),
        pf86 + QStringLiteral("/Google/Chrome/Application/chrome.exe"),
        lad + QStringLiteral("/Google/Chrome/Application/chrome.exe"),
    };
}

QString CdpLoginService::findBrowser()
{
    for (const QString& p : browserCandidates()) {
        if (QFile::exists(p))
            return p;
    }
    return {};
}

CdpLoginService::CdpLoginService(QObject* parent)
    : QObject(parent)
{
    m_browserPath = findBrowser();
}

void CdpLoginService::login(const CdpLoginRequest& req)
{
    loginBulk({req}, 1);
}

void CdpLoginService::loginBulk(const QList<CdpLoginRequest>& requests, int concurrency)
{
    if (requests.isEmpty())
        return;
    m_concurrency = qMax(1, concurrency);
    m_total += requests.size();
    m_queue.append(requests);
    pumpQueue();
}

void CdpLoginService::pumpQueue()
{
    if (m_browserPath.isEmpty()) {
        // No browser found — every request fails
        for (const auto& r : std::as_const(m_queue))
            emit loginFailed(r.username, QStringLiteral("browser-not-found"));
        m_queue.clear();
        if (m_active.isEmpty() && m_total > 0) {
            emit allFinished();
            m_total = m_succeeded = m_failed = 0;
        }
        return;
    }
    while (m_active.size() < m_concurrency && !m_queue.isEmpty()) {
        const CdpLoginRequest req = m_queue.takeFirst();

        // Window grid: 2 columns (like RAM), at most 2 rows per screen
        QRect cell;
        const int index = m_succeeded + m_failed + m_active.size();
        const QRect wa = ex::platform::WindowMgr::primaryWorkArea();
        if (wa.isValid()) {
            const int cols = 2;
            const int rowsOnScreen = 2;
            const int w = wa.width() / cols;
            const int h = wa.height() / rowsOnScreen;
            const int col = index % cols;
            const int row = (index / cols) % rowsOnScreen;
            cell = QRect(wa.left() + col * w, wa.top() + row * h, w, h);
        }

        auto* session = new CdpLoginSession(this);
        m_active.append(session);
        connect(session, &CdpLoginSession::statusChanged, this,
                [this, username = req.username](Status s, const QString& d) {
            emit sessionStatus(username, static_cast<int>(s), d);
        });
        connect(session, &CdpLoginSession::captured, this,
                [this](const QString& u, const QString& c, const QString& p,
                       qint64 userId, const QString& realUsername,
                       const QString& realDisplayName) {
            ++m_succeeded;
            emit loginSucceeded(u, c, p, userId, realUsername, realDisplayName);
        });
        connect(session, &CdpLoginSession::finished, this, [this, session]() {
            m_active.removeOne(session);
            session->deleteLater();
            pumpQueue();
            if (m_active.isEmpty() && m_queue.isEmpty()) {
                EXLOG_INFO("cdp") << "all login sessions finished:"
                                  << m_succeeded << "ok," << (m_total - m_succeeded) << "failed";
                emit allFinished();
                m_total = m_succeeded = m_failed = 0;
            }
        });
        connect(session, &CdpLoginSession::failedInternal, this,
                [this, username = req.username](const QString& detail) {
            ++m_failed;
            emit loginFailed(username, detail.isEmpty() ? QStringLiteral("session-failed") : detail);
        });
        session->start(m_browserPath, req, cell);
    }
}

void CdpLoginService::cancelAll()
{
    m_queue.clear();
    const auto active = m_active;
    for (CdpLoginSession* s : active)
        s->cancel();
}

int CdpLoginService::activeCount() const
{
    return m_active.size();
}

} // namespace ex::roblox

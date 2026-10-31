#include "core/launcher/launch_service.h"

#include "core/launcher/launch_uri.h"
#include "core/util/log.h"
#include "core/util/redact.h"
#include "platform/windows/process.h"
#include "platform/windows/ram_limits.h"
#include "platform/windows/roblox_install.h"
#include "platform/windows/window_mgr.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTimer>

#include <algorithm>
#include <memory>

namespace ex::launcher {

using ex::model::LaunchTarget;

namespace {

QString chooseBestServer(const QList<ex::roblox::ServerEntry>& servers)
{
    // «Лучший публичный»: максимум игроков, но не заполненный (RAM/Astro).
    QString best;
    int bestPlayers = -1;
    for (const auto& s : servers) {
        if (s.jobId.isEmpty() || s.maxPlayers <= 0 || s.playerCount >= s.maxPlayers)
            continue;
        if (s.playerCount > bestPlayers) {
            bestPlayers = s.playerCount;
            best = s.jobId;
        }
    }
    return best;
}

QString chooseRandomServer(const QList<ex::roblox::ServerEntry>& servers)
{
    QList<QString> candidates;
    for (const auto& s : servers) {
        if (!s.jobId.isEmpty() && s.maxPlayers > 0 && s.playerCount < s.maxPlayers)
            candidates.append(s.jobId);
    }
    if (candidates.isEmpty())
        return {};
    return candidates.at(QRandomGenerator::global()->bounded(candidates.size()));
}

// Окно Roblox появляется через несколько секунд после старта процесса:
// ждём его и повторяем профиль ещё пару раз — загрузчик может переустановить
// размер окна после splash-экрана.
void applyProfileWhenReady(QObject* context, qint64 pid,
                           const ex::model::WindowProfile& profile,
                           int waitLeft, int settleLeft)
{
    if (ex::platform::WindowMgr::applyProfile(pid, profile)) {
        if (settleLeft <= 0)
            return;
        QTimer::singleShot(700, context,
                           [context, pid, profile, settleLeft]() {
            applyProfileWhenReady(context, pid, profile, 0, settleLeft - 1);
        });
        return;
    }
    if (waitLeft <= 0) {
        EXLOG_WARN("launcher") << "window did not appear for pid" << pid;
        return;
    }
    QTimer::singleShot(400, context,
                       [context, pid, profile, waitLeft, settleLeft]() {
        applyProfileWhenReady(context, pid, profile, waitLeft - 1, settleLeft);
    });
}

} // namespace

LaunchService::LaunchService(ex::roblox::RobloxApi& api,
                             ex::store::AccountStore& store,
                             ex::store::Vault& vault,
                             ex::instances::InstanceRegistry& registry,
                             QObject* parent)
    : QObject(parent)
    , m_api(api)
    , m_store(store)
    , m_vault(vault)
    , m_registry(registry)
{
    m_delayTimer.setSingleShot(true);
    connect(&m_delayTimer, &QTimer::timeout, this, &LaunchService::pumpQueue);

    connect(&m_registry, &ex::instances::InstanceRegistry::instanceStarted,
            this, [this](const ex::model::InstanceInfo& info) {
        const auto accOpt = m_store.accountById(info.accountId);
        if (!accOpt)
            return;
        const auto& acc = *accOpt;

        // Окно: размер, выбранный в панели запуска для этого batch, важнее
        // per-account profile из fields["_window"].
        ex::model::WindowProfile wp;
        if (m_launchWindows.contains(info.accountId)) {
            wp = m_launchWindows.take(info.accountId);
        } else if (acc.fields.contains(QStringLiteral("_window"))) {
            const QJsonObject o = QJsonDocument::fromJson(
                acc.fields.value(QStringLiteral("_window")).toString().toUtf8()).object();
            wp = ex::model::WindowProfile::fromJson(o);
        }
        if (wp.enabled)
            applyProfileWhenReady(this, info.pid, wp, 20, 2);

        // RAM limit [№1]
        ex::model::RamLimit rl;
        if (acc.fields.contains(QStringLiteral("_ram"))) {
            const QJsonObject o = QJsonDocument::fromJson(
                acc.fields.value(QStringLiteral("_ram")).toString().toUtf8()).object();
            rl = ex::model::RamLimit::fromJson(o);
        }
        switch (rl.mode) {
        case ex::model::RamLimit::Mode::Soft:
            ex::platform::RamLimiter::trimWorkingSet(info.pid);
            ex::platform::RamLimiter::lowerPriority(info.pid);
            break;
        case ex::model::RamLimit::Mode::Hard:
            ex::platform::RamLimiter::applyHardLimit(info.pid, rl.limitMb);
            break;
        default:
            break;
        }

        // Ledger сессии для аналитики [№6]: place/job — из фактического запуска.
        if (!m_ledgerIds.contains(info.accountId)) {
            const LaunchTarget target = m_launchTargets.value(info.accountId);
            qint64 ledgerId = 0;
            if (m_store.sessionStart(info.accountId, target.placeId, target.jobId,
                                     info.pid, ledgerId)) {
                m_ledgerIds.insert(info.accountId, ledgerId);
            }
        }
    });

    connect(&m_registry, &ex::instances::InstanceRegistry::instanceCrashed,
            this, [this](const ex::model::InstanceInfo& info) {
        const qint64 ledgerId = m_ledgerIds.take(info.accountId);
        if (ledgerId)
            m_store.sessionEnd(ledgerId, QStringLiteral("closed"));
    });
}

LaunchService::~LaunchService()
{
    // Нормальный выход: закрываем активные сессии временем выхода, чтобы
    // ledger не считал простой после перезапуска.
    for (auto it = m_ledgerIds.constBegin(); it != m_ledgerIds.constEnd(); ++it) {
        if (it.value())
            m_store.sessionEnd(it.value(), QStringLiteral("app-exit"));
    }
    m_ledgerIds.clear();
}

void LaunchService::launch(qint64 accountId, const LaunchTarget& target,
                           const LaunchOptions& options)
{
    BatchRequest request;
    request.accountIds = {accountId};
    request.target = target;
    request.mode = BatchMode::SameServer;
    request.options = options;
    request.concurrency = 1;
    request.delaySeconds = 0;
    launchBatch(request);
}

void LaunchService::launchBatch(const BatchRequest& request)
{
    if (request.accountIds.isEmpty()) {
        emit batchFinished(0, 0);
        return;
    }

    m_concurrency = std::clamp(request.concurrency, 1, 8);
    m_delayMs = std::max(0, request.delaySeconds) * 1000;

    // RM: перед запуском стираем RobloxCookies.dat, чтобы клиент не
    // ассоциировал аккаунты между запусками.
    if (!ex::platform::clearRobloxCookieFile()) {
        EXLOG_WARN("launcher")
            << "could not clear RobloxCookies.dat before launch";
    }

    if (!m_vault.isUnlocked()) {
        // Без vault нет cookie: честно сообщаем по каждому аккаунту.
        for (qint64 id : request.accountIds) {
            LaunchResult r;
            r.accountId = id;
            r.ok = false;
            r.error = QStringLiteral("vault-locked");
            emit launchFinished(r);
        }
        emit batchFinished(0, request.accountIds.size());
        return;
    }

    QList<Job> jobs;
    jobs.reserve(request.accountIds.size());
    const bool isShareLink = request.target.kind == LaunchTarget::Kind::ShareLink;
    for (qint64 id : request.accountIds) {
        Job job;
        job.accountId = id;
        job.target = request.target;
        job.options = request.options;
        if (!isShareLink) {
            switch (request.mode) {
            case BatchMode::SameServer:
                break;
            case BatchMode::DifferentServers:
                job.target.kind = LaunchTarget::Kind::Place;
                job.target.jobId.clear();
                job.randomServer = true;
                break;
            case BatchMode::PrivateServer:
                job.target.kind = LaunchTarget::Kind::PrivateServer;
                break;
            }
        }
        jobs.append(job);
    }

    m_batchTotal = static_cast<int>(jobs.size());
    m_batchDone = 0;
    m_batchOk = 0;
    m_batchFail = 0;

    // SameServer без jobId: выбрать ОДИН сервер на всю пачку (не случайный
    // на каждый аккаунт, иначе режим «один сервер» врёт).
    const bool needSameServerPick = !isShareLink
        && request.mode == BatchMode::SameServer
        && jobs.first().target.kind == LaunchTarget::Kind::Place
        && jobs.first().target.placeId > 0;
    if (needSameServerPick) {
        QString cookie;
        for (qint64 id : request.accountIds) {
            cookie = m_vault.cookie(id);
            if (!cookie.isEmpty())
                break;
        }
        if (!cookie.isEmpty()) {
            const qint64 placeId = jobs.first().target.placeId;
            m_api.publicServers(
                cookie, placeId,
                [this, jobs = std::move(jobs)](bool ok,
                                               const QList<ex::roblox::ServerEntry>& servers,
                                               const QString&) mutable {
                    if (ok) {
                        const QString jobId = chooseBestServer(servers);
                        if (!jobId.isEmpty()) {
                            for (auto& job : jobs) {
                                job.target.kind = LaunchTarget::Kind::Job;
                                job.target.jobId = jobId;
                            }
                        }
                    }
                    if (ok && !jobs.first().target.jobId.isEmpty()) {
                        EXLOG_INFO("launcher") << "same-server pick:"
                                               << jobs.first().target.jobId;
                    }
                    enqueueJobs(std::move(jobs));
                });
            return;
        }
    }
    enqueueJobs(std::move(jobs));
}

void LaunchService::enqueueJobs(QList<Job> jobs)
{
    for (const Job& job : jobs) {
        if (job.options.window.enabled)
            m_launchWindows.insert(job.accountId, job.options.window);
        else
            m_launchWindows.remove(job.accountId);
        m_queue.enqueue(job);
        emit launchQueued(job.accountId);
    }
    pumpQueue();
}

void LaunchService::cancelQueue()
{
    m_queue.clear();
    EXLOG_INFO("launcher") << "queue cancelled";
}

void LaunchService::pumpQueue()
{
    while (m_active < m_concurrency && !m_queue.isEmpty()) {
        const Job job = m_queue.dequeue();
        ++m_active;
        runJob(job);
    }
}

void LaunchService::runJob(const Job& job)
{
    auto finished = std::make_shared<bool>(false);
    auto done = [this, job, finished](bool ok, const QString& error, const QString& token) {
        if (*finished)
            return;
        *finished = true;
        finishJob(job, ok, error, token);
    };

    const auto accOpt = m_store.accountById(job.accountId);
    if (!accOpt) {
        done(false, QStringLiteral("account-not-found"), QString());
        return;
    }
    if (!m_vault.isUnlocked()) {
        done(false, QStringLiteral("vault-locked"), QString());
        return;
    }
    const QString cookie = m_vault.cookie(job.accountId);
    if (cookie.isEmpty()) {
        done(false, QStringLiteral("no-session"), QString());
        return;
    }

    auto proceed = [this, job, cookie, done](LaunchTarget target) {
        if (!target.isValid()) {
            done(false, QStringLiteral("bad-target"), QString());
            return;
        }

        auto runTicket = [this, job, target, done](const QString& cookieForTicket) {
            m_api.generateAuthTicket(
                cookieForTicket,
                [this, job, target, done](bool ok, const QString& ticket,
                                          const QString& err,
                                          const QString& effectiveCookie) {
                    Q_UNUSED(effectiveCookie); // ротацию уже сохранил cookieRotated
                    if (!ok) {
                        if (err == QLatin1String("cookie-rejected"))
                            emit sessionInvalid(job.accountId, err);
                        done(false, err, QString());
                        return;
                    }

                    const QString launchToken = LaunchUri::newLaunchToken();
                    qint64 trackerId = m_trackerIds.value(job.accountId);
                    if (!trackerId) {
                        trackerId = LaunchUri::newBrowserTrackerId();
                        m_trackerIds.insert(job.accountId, trackerId);
                    }
                    const QString uri = LaunchUri::build(target, ticket, launchToken, trackerId);
                    if (uri.isEmpty()) {
                        done(false, QStringLiteral("bad-target"), launchToken);
                        return;
                    }

                    // FPS-лимит не применяем: клиент работает без капа.

                    // Регистрируем ожидание ДО hand-off в Windows (анти-сирота, Astro)
                    m_registry.expectLaunch(job.accountId, launchToken);

                    if (job.options.closePreviousInstance) {
                        const auto prev = m_registry.byAccount(job.accountId);
                        if (prev && prev->pid) {
                            EXLOG_INFO("launcher") << "closing previous instance pid"
                                                   << prev->pid;
                            ex::platform::requestGracefulClose(prev->pid);
                        }
                    }

                    // Выбранный в настройках клиент важнее системной
                    // ассоциации. Если путь устарел/пропал — откатываемся
                    // на зарегистрированный обработчик, как RM.
                    const QString selectedExe =
                        ex::platform::resolveRobloxPlayerPath(job.options.robloxPath);
                    if (!job.options.robloxPath.isEmpty() && selectedExe.isEmpty()) {
                        EXLOG_WARN("launcher")
                            << "configured Roblox client no longer resolves;"
                            << "using registered handler";
                    }
                    bool opened = false;
                    if (!selectedExe.isEmpty()) {
                        EXLOG_INFO("launcher") << "using configured Roblox client"
                                               << selectedExe;
                        opened = ex::platform::launchPlayerDeElevated(selectedExe, uri);
                        if (!opened) {
                            EXLOG_WARN("launcher")
                                << "configured Roblox client failed; falling back"
                                << "to registered handler";
                        }
                    }
                    if (!opened)
                        opened = ex::platform::openUriDeElevated(uri);
                    if (!opened) {
                        m_registry.giveUpLaunch(job.accountId, launchToken);
                        done(false, QStringLiteral("shell-failed"), launchToken);
                        return;
                    }

                    m_store.markSessionValid(job.accountId);
                    m_launchTargets.insert(job.accountId, target);
                    m_store.touchAccountUsed(job.accountId);
                    m_store.logActivity(QStringLiteral("launch"), job.accountId,
                                        target.describe());
                    EXLOG_INFO("launcher") << "launched account" << job.accountId
                                           << "target" << target.describe();
                    done(true, QString(), launchToken);
                });
        };

        // Тикет получаем тем же способом, что и RAM: cookie из vault,
        // CSRF-челлендж обрабатывает authedPost, ротации Set-Cookie — общий
        // cookieRotated-механизм.
        runTicket(cookie);
    };

    if (job.target.kind == LaunchTarget::Kind::ShareLink) {
        // Резолвим share-ссылку от имени целевого аккаунта (приём Astro v12)
        m_api.resolveShareLink(
            cookie, job.target.shareCode,
            [this, job, proceed, done](bool ok, ex::roblox::ShareLinkInfo info,
                                       const QString& err) {
                if (!ok) {
                    if (err == QLatin1String("cookie-rejected"))
                        emit sessionInvalid(job.accountId, err);
                    done(false, err, QString());
                    return;
                }
                LaunchTarget target = job.target;
                target.kind = LaunchTarget::Kind::PrivateServer;
                target.placeId = info.placeId;
                target.privateServerCode = info.privateServerCode;
                target.shareCode.clear();
                proceed(target);
            });
        return;
    }

    if (job.randomServer && job.target.kind == LaunchTarget::Kind::Place) {
        m_api.publicServers(
            cookie, job.target.placeId,
            [this, job, proceed](bool ok,
                                 const QList<ex::roblox::ServerEntry>& servers,
                                 const QString&) {
                LaunchTarget target = job.target;
                if (ok) {
                    const QString jobId = chooseRandomServer(servers);
                    if (!jobId.isEmpty()) {
                        target.kind = LaunchTarget::Kind::Job;
                        target.jobId = jobId;
                    }
                }
                proceed(target);
            });
        return;
    }

    proceed(job.target);
}

void LaunchService::finishJob(const Job& job, bool ok, const QString& error,
                              const QString& token)
{
    --m_active;
    if (ok) {
        ++m_batchOk;
    } else {
        ++m_batchFail;
        m_launchWindows.remove(job.accountId);
    }
    ++m_batchDone;

    LaunchResult result;
    result.accountId = job.accountId;
    result.ok = ok;
    result.error = error;
    result.launchToken = token;
    emit launchFinished(result);
    emit batchProgress(m_batchDone, m_batchTotal);

    if (m_active == 0 && m_queue.isEmpty() && m_batchTotal > 0
        && m_batchDone >= m_batchTotal) {
        emit batchFinished(m_batchOk, m_batchFail);
        m_batchTotal = 0;
    }

    if (!m_queue.isEmpty() && m_active < m_concurrency) {
        if (m_delayMs > 0) {
            if (!m_delayTimer.isActive())
                m_delayTimer.start(m_delayMs);
        } else {
            // Не рекурсируем из runJob, если он завершился синхронно.
            QTimer::singleShot(0, this, &LaunchService::pumpQueue);
        }
    }
}

bool LaunchService::closeInstance(qint64 accountId, bool graceful)
{
    const auto info = m_registry.byAccount(accountId);
    if (!info || !info->pid)
        return false;
    if (graceful)
        return ex::platform::requestGracefulClose(info->pid);
    // Kill-verified: имя образа + launchtime-токен (тройная проверка, RM)
    return ex::platform::killVerified(info->pid, QStringLiteral("RobloxPlayerBeta.exe"),
                                      info->launchToken);
}

int LaunchService::closeAll()
{
    int closed = 0;
    // Только атрибутированные нами инстансы: чужие клиенты не убиваем.
    for (const auto& inst : m_registry.instances()) {
        if (!inst.pid)
            continue;
        if (ex::platform::killVerified(inst.pid, QStringLiteral("RobloxPlayerBeta.exe"),
                                       inst.launchToken)) {
            ++closed;
        }
    }
    return closed;
}

} // namespace ex::launcher

#include "core/util/log.h"

#include "core/util/redact.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

namespace ex::log {

namespace {
struct LogState {
    QMutex mutex;
    QString logsDir;
    QFile file;
    QDate fileDate;
    int keepDays = 7;
    bool consoleEcho = false;
    bool scrub = true;
};
LogState g_state;
} // namespace

void init(const QString& logsDir, int keepDays)
{
    QMutexLocker lock(&g_state.mutex);
    g_state.logsDir = logsDir;
    g_state.keepDays = keepDays;
    g_state.file.close();
    g_state.fileDate = QDate();
}

void shutdown()
{
    QMutexLocker lock(&g_state.mutex);
    g_state.file.close();
}

bool initialized()
{
    QMutexLocker lock(&g_state.mutex);
    return !g_state.logsDir.isEmpty();
}

void setConsoleEcho(bool on)
{
    QMutexLocker lock(&g_state.mutex);
    g_state.consoleEcho = on;
}

namespace {
void purgeOldLogs_locked()
{
    // rm.<YYYY-MM-DD>.log — удалить старее keepDays
    const QString pattern = QStringLiteral("exmanager.*.log");
    const QStringList entries = QDir(g_state.logsDir).entryList({pattern}, QDir::Files, QDir::Name);
    const QDate threshold = QDate::currentDate().addDays(-g_state.keepDays);
    for (const QString& name : entries) {
        const QDate d = QDate::fromString(name.section(QLatin1Char('.'), 1, 1), QStringLiteral("yyyy-MM-dd"));
        if (d.isValid() && d < threshold)
            QFile::remove(g_state.logsDir + QLatin1Char('/') + name);
    }
}
} // namespace

void write(Level level, const QString& category, const QString& message)
{
    QMutexLocker lock(&g_state.mutex);
    if (g_state.logsDir.isEmpty())
        return;

    const QDateTime now = QDateTime::currentDateTime();

    // Ежедневная ротация
    if (!g_state.fileDate.isValid() || g_state.fileDate != now.date()) {
        g_state.file.close();
        QDir().mkpath(g_state.logsDir);
        const QString name = QStringLiteral("exmanager.%1.log").arg(now.date().toString(QStringLiteral("yyyy-MM-dd")));
        g_state.file.setFileName(g_state.logsDir + QLatin1Char('/') + name);
        (void)g_state.file.open(QIODevice::Append | QIODevice::Text); // при неудаче просто пишем в никуда — проверено выше
        g_state.fileDate = now.date();
        purgeOldLogs_locked();
    }

    // Backstop-скраббинг: НИ ОДНО сообщение не попадает на диск без scrub()
    const QString safe = ex::util::redact::scrub(message);
    static const char* kLevels[] = {"DEBUG", "INFO ", "WARN ", "ERROR"};
    const QString line = QStringLiteral("%1 [%2] [%3] %4\n")
        .arg(now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
             QLatin1String(kLevels[static_cast<int>(level)]),
             category, safe);

    if (g_state.file.isOpen()) {
        g_state.file.write(line.toUtf8());
        (void)g_state.file.flush(); // сброс на диск при каждой записи — секции лога не теряются при краше
    }
    if (g_state.consoleEcho)
        QTextStream(stderr) << line;
}

namespace detail {
LogStream::~LogStream()
{
    write(m_level, m_category, m_parts.join(QLatin1Char(' ')));
}
} // namespace detail

} // namespace ex::log

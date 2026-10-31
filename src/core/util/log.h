#pragma once

#include <QString>
#include <QStringList>

#include <memory>

namespace ex::log {

// Logger that always scrubs secrets (backstop) and rotates
// daily log rotation. Usage:
//
//   ex::log::info("launcher") << "launching" << accountId;
//   EXLOG_WARN("store") << "missing table, migrating";
//
// All messages pass through redact::scrub() at the sink level.

enum class Level { Debug, Info, Warn, Error };

void init(const QString& logsDir, int keepDays = 7);
void shutdown();
bool initialized();

// Also log to stderr (debugging; handy when starting a windowed build from a console).
void setConsoleEcho(bool on);

// The actual write (used by the stream below).
void write(Level level, const QString& category, const QString& message);

namespace detail {
// Stream wrapper: collects arguments, writes on destruction.
class LogStream {
public:
    LogStream(Level level, QString category)
        : m_level(level), m_category(std::move(category)) {}
    ~LogStream();
    LogStream& operator<<(const QString& v) { m_parts << v; return *this; }
    LogStream& operator<<(QStringView v) { m_parts << v.toString(); return *this; }
    LogStream& operator<<(const char* v) { m_parts << QString::fromUtf8(v); return *this; }
    LogStream& operator<<(qlonglong v) { m_parts << QString::number(v); return *this; }
    LogStream& operator<<(qulonglong v) { m_parts << QString::number(v); return *this; }
    LogStream& operator<<(int v) { m_parts << QString::number(v); return *this; }
    LogStream& operator<<(unsigned v) { m_parts << QString::number(v); return *this; }
    LogStream& operator<<(long v) { m_parts << QString::number(v); return *this; }
    LogStream& operator<<(unsigned long v) { m_parts << QString::number(v); return *this; }
    LogStream& operator<<(double v) { m_parts << QString::number(v, 'f', 3); return *this; }
    LogStream& operator<<(bool v) { m_parts << (v ? QStringLiteral("true") : QStringLiteral("false")); return *this; }
private:
    Level m_level;
    QString m_category;
    QStringList m_parts;
};
} // namespace detail

inline detail::LogStream debug(const QString& category) { return {Level::Debug, category}; }
inline detail::LogStream info(const QString& category) { return {Level::Info, category}; }
inline detail::LogStream warn(const QString& category) { return {Level::Warn, category}; }
inline detail::LogStream error(const QString& category) { return {Level::Error, category}; }

#define EXLOG_DEBUG(cat) ::ex::log::debug(cat)
#define EXLOG_INFO(cat)  ::ex::log::info(cat)
#define EXLOG_WARN(cat)  ::ex::log::warn(cat)
#define EXLOG_ERROR(cat) ::ex::log::error(cat)

} // namespace ex::log

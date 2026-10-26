#pragma once

#include <QString>
#include <QStringList>

#include <memory>

namespace ex::log {

// Логгер с обязательным скраббингом секретов (backstop) и ежедневной
// ротацией файлов. Использование:
//
//   ex::log::info("launcher") << "launching" << accountId;
//   EXLOG_WARN("store") << "missing table, migrating";
//
// Все сообщения проходят через redact::scrub() на уровне sink'а.

enum class Level { Debug, Info, Warn, Error };

void init(const QString& logsDir, int keepDays = 7);
void shutdown();
bool initialized();

// Включить вывод в stderr (для отладки; в windowed-режиме полезно при запуске из консоли).
void setConsoleEcho(bool on);

// Собственно запись (используется стримом ниже).
void write(Level level, const QString& category, const QString& message);

namespace detail {
// Стрим-обёртка: копит аргументы, пишет при разрушении.
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

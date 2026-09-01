#include "logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace burninsys {

static QString levelToString(Logger::Level lvl)
{
    switch (lvl)
    {
    case Logger::Level::Trace: return "TRACE";
    case Logger::Level::Debug: return "DEBUG";
    case Logger::Level::Info:  return "INFO";
    case Logger::Level::Warn:  return "WARN";
    case Logger::Level::Error: return "ERROR";
    case Logger::Level::Fatal: return "FATAL";
    }
    return "UNK";
}

Logger::Logger()
    : m_level(Level::Info),
      m_stream(&m_file)
{
    m_stream.setEncoding(QStringConverter::Utf8);
    // 默认 Sink：控制台输出
    addSink([](const QString &line) { qInfo().noquote() << line; });
}

void Logger::setLevel(Level level)
{
    QMutexLocker locker(&m_mutex);
    m_level = level;
}

Logger::Level Logger::level() const
{
    return m_level;
}

void Logger::enableFileSink(const QString &path)
{
    QMutexLocker locker(&m_mutex);
    if (m_file.isOpen())
    {
        m_file.close();
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::Append | QIODevice::Text))
    {
        qWarning() << "Logger: failed to open file" << path;
    }
    m_stream.setDevice(&m_file);
}

void Logger::disableFileSink()
{
    QMutexLocker locker(&m_mutex);
    if (m_file.isOpen())
    {
        m_file.close();
    }
}

void Logger::addSink(Sink sink)
{
    QMutexLocker locker(&m_mutex);
    m_sinks.append(std::move(sink));
}

QString Logger::format(Level level, const QString &msg) const
{
    const auto ts = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return QStringLiteral("[%1] [%2] %3").arg(ts, levelToString(level), msg);
}

void Logger::emitLine(Level level, const QString &msg)
{
    // B11：级别判断挪进锁内 —— 旧代码在锁外读 m_level，setLevel（带锁）并发时是
    // TSAN 级数据竞争（ca96461 竞争修复的漏网之鱼）。锁内读+写统一序列化。
    QMutexLocker locker(&m_mutex);
    if (level < m_level)
        return;

    const auto line = format(level, msg);
    for (const auto &sink : std::as_const(m_sinks))
    {
        sink(line);
    }
    writeToFile(line);
}

void Logger::writeToFile(const QString &line)
{
    if (!m_file.isOpen())
        return;

    m_stream << line << "\n";
    m_stream.flush();
}

void Logger::trace(const QString &msg) { emitLine(Level::Trace, msg); }
void Logger::debug(const QString &msg) { emitLine(Level::Debug, msg); }
void Logger::info(const QString &msg)  { emitLine(Level::Info, msg); }
void Logger::warn(const QString &msg)  { emitLine(Level::Warn, msg); }
void Logger::error(const QString &msg) { emitLine(Level::Error, msg); }
void Logger::fatal(const QString &msg) { emitLine(Level::Fatal, msg); }

} // namespace burninsys

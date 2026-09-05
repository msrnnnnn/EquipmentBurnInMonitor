#pragma once

#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>
#include <QString>
#include <QList>
#include <functional>

namespace burninsys {

// 单例 + Sink机制 + 互斥锁线程安全
// 单向依赖：所有模块依赖Logger，Logger不依赖其他单例，梅耶斯单例安全
// Sink = 可插拔的输出目标（控制台、文件、UI状态栏），开闭原则
class Logger
{
public:
    enum class Level
    {
        Trace = 0,
        Debug,
        Info,
        Warn,
        Error,
        Fatal
    };

    // Sink = 输出回调，用 std::function 实现多态，比接口类更轻量
    using Sink = std::function<void(const QString&)>;

    // 梅耶斯单例：局部静态变量，线程安全（C++11保证），只初始化一次
    static Logger& instance()
    {
        static Logger inst;
        return inst;
    }

    // 禁止拷贝和赋值（单例的标配）
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void setLevel(Level level);
    Level level() const;

    void enableFileSink(const QString &path);
    void disableFileSink();
    void addSink(Sink sink);

    void trace(const QString &msg);
    void debug(const QString &msg);
    void info(const QString &msg);
    void warn(const QString &msg);
    void error(const QString &msg);
    void fatal(const QString &msg);

private:
    Logger();
    QString format(Level level, const QString &msg) const;
    void emitLine(Level level, const QString &msg);
    void writeToFile(const QString &line);

    Level m_level;
    QFile m_file;
    QTextStream m_stream;
    QList<Sink> m_sinks;
    QMutex m_mutex;
};

} // namespace burninsys

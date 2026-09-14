#include "SqliteRepository.h"
#include "logging/Logger.h"
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QThread>

using burninsys::Logger;

SqliteRepository::SqliteRepository(QObject *parent) : QObject(parent) {}

SqliteRepository::~SqliteRepository()
{
    // 顺序不能反：先关连接、再把句柄变量置空（让它释放对连接对象的最后一个引用），
    // 最后才能 removeDatabase。少掉中间那步，Qt 会打
    // "QSqlDatabasePrivate::removeDatabase: connection 'xxx' is still in use"
    // 并且连接其实没被真正回收 —— 热更新反复 addDatabase 时就会撞上重复连接名。
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase();
    // 防御：open() 从未执行过时（如 start 后立刻 stop，队列里的 open 被丢弃），
    // 连接名是空串，removeDatabase("") 会误伤默认连接，这里直接跳过。
    if (!m_connectionName.isEmpty())
        QSqlDatabase::removeDatabase(m_connectionName);
}

void SqliteRepository::open(const QString &dbPath)
{
    // 用独立连接名，不跟其他线程冲突
    m_connectionName = QStringLiteral("repository_%1").arg(reinterpret_cast<quintptr>(this));
    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    m_db.setDatabaseName(dbPath);
    m_dbPath = dbPath;

    QDir().mkpath(QFileInfo(dbPath).absolutePath());

    if (!m_db.open()) {
        Logger::instance().error("SQLite open failed");
        return;
    }

    // 配置 WAL 模式
    QSqlQuery query(m_db);
    query.exec("PRAGMA journal_mode=WAL;");
    query.exec("PRAGMA busy_timeout=5000;");
    query.exec("PRAGMA synchronous=NORMAL;");

    // 建表
    query.exec(R"(
        CREATE TABLE IF NOT EXISTS telemetry (
            id      INTEGER PRIMARY KEY AUTOINCREMENT,
            ts_ms   INTEGER NOT NULL,
            name    TEXT    NOT NULL,
            value   REAL,
            quality TEXT    DEFAULT 'good'
        )
    )");
    // 三个索引各管一种查询形状：
    //   idx_telemetry_ts   全量按时间扫
    //   idx_telemetry_name 按指标名聚合
    //   idx_name_ts        query() 的 name + 时间区间（复合索引，最常用）
    query.exec("CREATE INDEX IF NOT EXISTS idx_telemetry_ts   ON telemetry(ts_ms)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_telemetry_name ON telemetry(name)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_name_ts ON telemetry(name, ts_ms)");
}

bool SqliteRepository::tryInsert(const TelemetrySample &sample)
{
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO telemetry (ts_ms, name, value, quality) VALUES (?, ?, ?, ?)");
    query.addBindValue(sample.timestampMs);
    query.addBindValue(sample.name);
    query.addBindValue(sample.value);
    query.addBindValue(sample.quality);
    return query.exec();
}

void SqliteRepository::save(const TelemetrySample &sample)
{
    for (int i = 0; i < 3; ++i) {
        if (tryInsert(sample))
            return;
        QThread::msleep(500);
    }
    cacheToFile(sample);
    Logger::instance().warn(QStringLiteral("SQLite write failed after 3 retries, cached to file: %1")
                                .arg(sample.name));
}

void SqliteRepository::cacheToFile(const TelemetrySample &sample)
{
    QFile f(m_dbPath + ".cache");
    if (!f.open(QIODevice::Append | QIODevice::Text))
        return;
    QTextStream ts(&f);
    ts << sample.timestampMs << "," << sample.name << ","
       << sample.value << "," << sample.quality << "\n";
}

QVector<TelemetrySample> SqliteRepository::query(const QString &name, qint64 fromMs, qint64 toMs) const
{
    QVector<TelemetrySample> results;
    QSqlQuery q(m_db);
    q.prepare("SELECT ts_ms, name, value, quality FROM telemetry WHERE name = ? AND ts_ms BETWEEN ? AND ?");
    q.addBindValue(name);
    q.addBindValue(fromMs);
    q.addBindValue(toMs);
    q.exec();

    while (q.next()) {
        TelemetrySample s;
        s.timestampMs = q.value(0).toLongLong();
        s.name        = q.value(1).toString();
        s.value       = q.value(2).toDouble();
        s.quality     = q.value(3).toString();
        results.append(s);
    }
    return results;
}

#include "SqliteRepository.h"
#include "logging/logger.h"

SqliteRepository::SqliteRepository() {}

void SqliteRepository::open(const QString &dbPath)
{
    // 用独立连接名，不跟其他线程冲突
    m_db = QSqlDatabase::addDatabase("QSQLITE", "repository");
    m_db.setDatabaseName(dbPath);
    m_dbPath = dbPath;

    if (!m_db.open()) {
        burninsys::Logger::instance().error("SQLite open failed");
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
            ts_ms   INTEGER NOT NULL,
            name    TEXT    NOT NULL,
            value   REAL    NOT NULL,
            quality TEXT    DEFAULT 'good'
        )
    )");
    query.exec("CREATE INDEX IF NOT EXISTS idx_name_ts ON telemetry(name, ts_ms)");
}

void SqliteRepository::save(const TelemetrySample &sample)
{
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO telemetry (ts_ms, name, value, quality) VALUES (?, ?, ?, ?)");
    query.addBindValue(sample.timestampMs);
    query.addBindValue(sample.name);
    query.addBindValue(sample.value);
    query.addBindValue(sample.quality);
    query.exec();
}

QVector<TelemetrySample> SqliteRepository::query(const QString &name, qint64 fromMs, qint64 toMs) const
{
    QVector<TelemetrySample> results;
    QSqlQuery q(m_db);
    q.prepare("SELECT ts_ms, name, value, quality FROM telemetry WHERE name = ? AND ts_ms BETWEEN ? AND ?");
    q.addBindValue(name);
    q.addBindValue(fromMs);
    q.addBindValue(toMs);

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

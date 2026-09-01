#ifndef SQLITEREPOSITORY_H
#define SQLITEREPOSITORY_H

#include "device/DeviceDriver.h"
#include <QSqlDatabase>
#include <QString>
#include <QSqlQuery>
#include <QObject>
class SqliteRepository : public QObject
{
    Q_OBJECT
public:
    explicit SqliteRepository(QObject *parent = nullptr);
    ~SqliteRepository() override;

    void open(const QString &dbPath);                              // 打开+建表+配置WAL
    QVector<TelemetrySample> query(const QString &name,qint64 fromMs, qint64 toMs) const;// SELECT历史

public slots:
    void save(const TelemetrySample &sample);                      // INSERT一条（失败重试 → 降级落文件）

private:
    bool tryInsert(const TelemetrySample &sample);                 // 单次插入尝试，成功返回 true
    void cacheToFile(const TelemetrySample &sample);               // 写库彻底失败时的兜底：追加到 .cache

    QSqlDatabase m_db;
    QString m_dbPath;
    QString m_connectionName;   // 析构时按名字回收连接，否则会泄漏并打 warning
};

#endif // SQLITEREPOSITORY_H

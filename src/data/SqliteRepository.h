#ifndef SQLITEREPOSITORY_H
#define SQLITEREPOSITORY_H

#include "device/DeviceDriver.h"
#include <QSqlDatabase>
#include <QString>
#include <QSqlQuery>
class SqliteRepository
{
public:
    SqliteRepository();
    void open(const QString &dbPath);                              // 打开+建表+配置WAL
    void save(const TelemetrySample &sample);                      // INSERT一条
    QVector<TelemetrySample> query(const QString &name,qint64 fromMs, qint64 toMs) const;// SELECT历史
private:
    QSqlDatabase m_db;
    QString m_dbPath;
};

#endif // SQLITEREPOSITORY_H

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
    SqliteRepository(QObject *parent = nullptr);
    void open(const QString &dbPath);                              // 打开+建表+配置WAL
    QVector<TelemetrySample> query(const QString &name,qint64 fromMs, qint64 toMs) const;// SELECT历史
public slots:
    void save(const TelemetrySample &sample);                      // INSERT一条
private:
    QSqlDatabase m_db;
    QString m_dbPath;
};

#endif // SQLITEREPOSITORY_H

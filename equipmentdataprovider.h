#ifndef EQUIPMENTDATAPROVIDER_H
#define EQUIPMENTDATAPROVIDER_H

#include <QObject>
#include <QVariant>

// [面试重点] 单一职责：只管历史缓存+信号广播，不做异常判断、不做数据库写入
// 异常判断归RuleEngine，数据库归SqliteWriter，各自独立
class EquipmentDataProvider : public QObject
{
    Q_OBJECT
public:
    explicit EquipmentDataProvider(QObject *parent = nullptr);
    QVariantList history() const;

public slots:
    void pushSample(QVariantMap record);
    void pushRealtime(QVariantMap record);
    void sendThreshold(double motorTemp, double current, double rpm, double vibration, double voltage, double power);
    void sendConnectRequest(const QString &host, int port);
signals:
    void historyUpdated(QVariantList history);
    void realtimeDataReady(QVariantMap record);
    void thresholdUpdated(double motorTemp, double current, double rpm, double vibration, double voltage, double power);
    void connectRequested(const QString &host, int port);
private:
    QVariantList m_history;
    int m_maxHistory;
};

#endif // EQUIPMENTDATAPROVIDER_H

#ifndef EQUIPMENTDATAPROVIDER_H
#define EQUIPMENTDATAPROVIDER_H

#include <QObject>
 #include <QVariant>
class EquipmentDataProvider : public QObject
{
    Q_OBJECT
public:
    explicit EquipmentDataProvider(QObject *parent = nullptr);

public slots:
    void pushSample(QVariantMap record);
    void pushRealtime(QVariantMap record);
signals:
    void historyUpdated(QVariantList history);
    void realtimeDataReady(QVariantMap record);
private:
    QVariantList m_history;
    int m_maxHistory;
};

#endif // EQUIPMENTDATAPROVIDER_H

#include "EquipmentDataProvider.h"

EquipmentDataProvider::EquipmentDataProvider(QObject *parent)
    : QObject{parent}, m_maxHistory(50)
{}

QVariantList EquipmentDataProvider::history() const
{
    return m_history;
}

void EquipmentDataProvider::pushSample(QVariantMap record)
{
    if(m_history.size() >= m_maxHistory){
        m_history.removeFirst();
    }
    m_history.append(record);
    emit historyUpdated(m_history);
}

void EquipmentDataProvider::pushRealtime(QVariantMap record)
{
    emit realtimeDataReady(record);
}

void EquipmentDataProvider::sendThreshold(double motorTemp, double current, double rpm, double vibration, double voltage, double power)
{
    emit thresholdUpdated(motorTemp, current, rpm, vibration, voltage, power);
}

void EquipmentDataProvider::sendConnectRequest(const QString &host, int port)
{
    emit connectRequested(host, port);
}

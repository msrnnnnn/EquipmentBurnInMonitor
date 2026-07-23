#include "equipmentdataprovider.h"

EquipmentDataProvider::EquipmentDataProvider(QObject *parent)
    : QObject{parent}, m_maxHistory(50)
{}

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

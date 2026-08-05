#ifndef SERVICEFACADE_H
#define SERVICEFACADE_H

#include "config/config.h"
#include "data/SqliteRepository.h"
#include "data/datacache.h"
#include "diagnostics/healthmonitor.h"
#include "equipmentdata.h"
#include "metrics/MetricsCollector.h"
#include "scheduler/PollingScheduler.h"
#include <QObject>
#include <QThread>

class ServiceFacade : public QObject
{
    Q_OBJECT
public:
    explicit ServiceFacade(QObject *parent = nullptr);
    ~ServiceFacade();
    void configure(const Config& config);
    void start(const Config &config);
    void stop();
    EquipmentData* getEquipmentData();
signals:
public slots:
    void onSampleReady(const TelemetrySample &s);
private:
    EquipmentData m_equipmentData;
    HealthMonitor* m_healthMonitor= nullptr;
    PollingScheduler* m_pollingScheduler = nullptr;
    ModbusSession* m_modbusSession = nullptr;
    MetricsCollector m_metricsCollector;
    DataCache m_cache;
    SqliteRepository m_sqliteRepository;
    QThread* m_workThread = nullptr;
    QList<DeviceDriver*> m_drivers;
};

#endif // SERVICEFACADE_H

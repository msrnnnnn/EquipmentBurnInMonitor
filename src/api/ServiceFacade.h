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
#include <QTimer>

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

    bool modbusConnected() const { return m_modbusConnected; }
    int telemetryCount() const { return m_telemetryCount; }
    bool isHealthy() const { return m_healthMonitor && m_healthMonitor->isHealthy(); }

signals:
    void modbusConnectedChanged(bool connected);
    void telemetryCountChanged(int count);
    void healthyChanged(bool healthy);

public slots:
    void onSampleReady(const TelemetrySample &s);

private:
    void refreshUiState();

    EquipmentData m_equipmentData;
    HealthMonitor* m_healthMonitor = nullptr;
    PollingScheduler* m_pollingScheduler = nullptr;
    ModbusSession* m_modbusSession = nullptr;
    MetricsCollector m_metricsCollector;
    DataCache m_cache;
    SqliteRepository* m_sqliteRepository = nullptr;
    QThread* m_workThread = nullptr;
    QList<DeviceDriver*> m_drivers;
    QTimer m_uiTimer;
    bool m_modbusConnected{false};
    int m_telemetryCount{0};
    QVariantMap m_pendingRecord;
};

#endif // SERVICEFACADE_H

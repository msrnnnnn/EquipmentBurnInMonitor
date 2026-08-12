#ifndef SERVICEFACADE_H
#define SERVICEFACADE_H

#include "config/ConfigWatcher.h"
#include "config/config.h"
#include "data/SqliteRepository.h"
#include "data/datacache.h"
#include "diagnostics/healthmonitor.h"
#include "equipmentdata.h"
#include "metrics/MetricsCollector.h"
#include "rules/RuleEngine.h"
#include "scheduler/PollingScheduler.h"
#include "test/TestRunner.h"
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
    void remainingSecondsChanged(int remainingSeconds);
    void testVerdictChanged(const QString &verdict);
    void motorStateChanged(bool running);
public slots:
    void onSampleReady(const TelemetrySample &s);
    void onThresholdUpdated(double motorTemp, double current, double rpm, double vibration, double voltage, double power);
    void onConfigUpdated(const Config &config);
    void onMotorCommand();
    void onModeChanged(int mode);
private:
    void rebuildRules(const Config &config);
    void refreshUiState();
    RuleEngine m_ruleEngine;
    EquipmentData m_equipmentData;
    HealthMonitor* m_healthMonitor = nullptr;
    PollingScheduler* m_pollingScheduler = nullptr;
    ModbusSession* m_modbusSession = nullptr;
    MetricsCollector m_metricsCollector;
    DataCache m_cache;
    SqliteRepository* m_sqliteRepository = nullptr;
    QThread* m_workThread = nullptr;
    QTimer m_uiTimer;
    bool m_modbusConnected{false};
    bool m_isRunning{false};
    int m_telemetryCount{0};
    QVariantMap m_pendingRecord;
    Config m_config;
    ConfigWatcher m_configWatcher;
    TestRunner m_testRunner;
};

#endif // SERVICEFACADE_H

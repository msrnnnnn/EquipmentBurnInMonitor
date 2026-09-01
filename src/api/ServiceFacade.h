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
#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
#include <QThread>
#include <QTimer>

// ════════════════════════════════════════════════════════════════════════
// ServiceFacade —— 门面 + 状态属性体系（S5）
//
// UI 层只认两类东西：
//   1. 信号（NOTIFY）：属性变化时发出，UI 用 lambda 订阅
//   2. getter（READ）：收到信号后主动读，避免信号里夹参数
//   这就是 Qt 属性系统的标准形态：Q_PROPERTY(READ, NOTIFY)。
// 属性统一由 refreshUiState（1s 定时器）做"边沿检测"后发出，
// 只有值真的变了才发信号 —— 否则 UI 每秒空转。
// ════════════════════════════════════════════════════════════════════════
class ServiceFacade : public QObject
{
    Q_OBJECT
    // ── 状态属性体系（S5）──
    Q_PROPERTY(QString serviceState READ serviceState NOTIFY serviceStateChanged)
    Q_PROPERTY(bool modbusConnected READ modbusConnected NOTIFY modbusConnectedChanged)
    Q_PROPERTY(bool schedulerActive READ schedulerActive NOTIFY schedulerActiveChanged)
    Q_PROPERTY(qint64 lastSampleAgeMs READ lastSampleAgeMs NOTIFY lastSampleAgeMsChanged)
    Q_PROPERTY(int telemetryCount READ telemetryCount NOTIFY telemetryCountChanged)
    Q_PROPERTY(QString lastRuleMessage READ lastRuleMessage NOTIFY lastRuleMessageChanged)
    Q_PROPERTY(QString lastRuleName READ lastRuleName NOTIFY lastRuleNameChanged)
    Q_PROPERTY(int triggeredRuleCount READ triggeredRuleCount NOTIFY triggeredRuleCountChanged)
    Q_PROPERTY(QString endpointHost READ endpointHost NOTIFY endpointChanged)
    Q_PROPERTY(int endpointPort READ endpointPort NOTIFY endpointChanged)
    Q_PROPERTY(QString endpointLabel READ endpointLabel NOTIFY endpointChanged)
    Q_PROPERTY(QString reconnectMessage READ reconnectMessage NOTIFY reconnectMessageChanged)
    Q_PROPERTY(bool isRunning READ isRunning NOTIFY isRunningChanged)
    Q_PROPERTY(QString startStopMessage READ startStopMessage NOTIFY startStopMessageChanged)
    Q_PROPERTY(double vibrationThreshold READ vibrationThreshold NOTIFY thresholdsChanged)
    Q_PROPERTY(double powerThreshold READ powerThreshold NOTIFY thresholdsChanged)
    Q_PROPERTY(double temperatureThreshold READ temperatureThreshold NOTIFY thresholdsChanged)
    Q_PROPERTY(double rpmThreshold READ rpmThreshold NOTIFY thresholdsChanged)
    Q_PROPERTY(QString thresholdWriteMessage READ thresholdWriteMessage NOTIFY thresholdWriteMessageChanged)
    Q_PROPERTY(int engineEvaluationCount READ engineEvaluationCount NOTIFY engineEvaluationCountChanged)
    Q_PROPERTY(int engineRuleCount READ engineRuleCount NOTIFY engineRuleCountChanged)
    Q_PROPERTY(QString lastEvaluationMetric READ lastEvaluationMetric NOTIFY lastEvaluationMetricChanged)
    Q_PROPERTY(QStringList recentRuleEvents READ recentRuleEvents NOTIFY recentRuleEventsChanged)
    Q_PROPERTY(int remainingSeconds READ remainingSeconds NOTIFY remainingSecondsChanged)
    Q_PROPERTY(QString testVerdict READ testVerdict NOTIFY testVerdictChanged)

public:
    explicit ServiceFacade(QObject *parent = nullptr);
    ~ServiceFacade();
    void configure(const Config& config);
    void start(const Config &config);
    void stop();
    EquipmentData* getEquipmentData();

    // ── 状态 getter（Q_PROPERTY READ，多数一行返回成员）──
    QString serviceState() const;                 // 三态判定，实现在 cpp
    bool modbusConnected() const { return m_modbusConnected; }
    bool schedulerActive() const { return m_schedulerActive; }
    qint64 lastSampleAgeMs() const { return m_lastSampleAgeMs; }
    int telemetryCount() const { return m_telemetryCount; }
    QString lastRuleMessage() const { return m_lastRuleMessage; }
    QString lastRuleName() const { return m_lastRuleName; }
    int triggeredRuleCount() const { return m_triggeredRuleCount; }
    QString endpointHost() const { return m_config.endpoint.host; }
    int endpointPort() const { return m_config.endpoint.port; }
    QString endpointLabel() const;                // "host:port"，实现在 cpp
    QString reconnectMessage() const { return m_reconnectMessage; }
    bool isRunning() const { return m_isRunning; }
    QString startStopMessage() const { return m_startStopMessage; }
    double vibrationThreshold() const { return m_vibrationThreshold; }
    double powerThreshold() const { return m_powerThreshold; }
    double temperatureThreshold() const { return m_temperatureThreshold; }
    double rpmThreshold() const { return m_rpmThreshold; }
    QString thresholdWriteMessage() const { return m_thresholdWriteMessage; }
    int engineEvaluationCount() const { return m_engineEvaluationCount; }
    int engineRuleCount() const { return m_engineRuleCount; }
    QString lastEvaluationMetric() const { return m_lastEvaluationMetric; }
    QStringList recentRuleEvents() const { return m_recentRuleEvents; }
    int remainingSeconds() const { return m_testRunner.remainingSeconds(); }
    QString testVerdict() const { return m_testRunner.verdict(); }

signals:
    // 全部无参 NOTIFY：UI 收到信号后用 getter 读值，避免"信号带参 + 属性"双通道
    void serviceStateChanged();
    void modbusConnectedChanged();
    void schedulerActiveChanged();
    void lastSampleAgeMsChanged();
    void telemetryCountChanged();
    void lastRuleMessageChanged();
    void lastRuleNameChanged();
    void triggeredRuleCountChanged();
    void endpointChanged();
    void reconnectMessageChanged();
    void isRunningChanged();
    void startStopMessageChanged();
    void thresholdsChanged();
    void thresholdWriteMessageChanged();
    void engineEvaluationCountChanged();
    void engineRuleCountChanged();
    void lastEvaluationMetricChanged();
    void recentRuleEventsChanged();
    void remainingSecondsChanged();
    void testVerdictChanged();

public slots:
    void onSampleReady(const TelemetrySample &s);
    void onThresholdUpdated(double motorTemp, double current, double rpm, double vibration, double voltage, double power);
    void onConfigUpdated(const Config &config);
    void onMotorCommand();
    void onModeChanged(int mode);
    // P0-4：设置页 Connect —— 换端点重启会话（异步，立即返回）
    void onConnectRequested(const QString &host, int port);
    // P0-4：设置页 Test Profile 应用 —— 更新测试规程，倒计时立即生效
    void applyTestProfile(const TestProfile &profile);
private:
    void rebuildRules(const Config &config);
    void refreshUiState();
    void updateAnomalyMode(const TelemetrySample &sample);   // S4：异常判定 → 高频采样
    void setThresholdSnapshot(double temp, double curr, double rpm, double vib, double volt, double power); // 阈值快照 + 消息
    void writeMotorStop();   // 自动停机联动：写停机线圈（不翻转 UI 状态，由调用方管理）
    void logMetricsSummary();   // B11：MetricsCollector 消费者 —— 每 60s 输出统计摘要

    RuleEngine m_ruleEngine;
    EquipmentData m_equipmentData;
    HealthMonitor* m_healthMonitor = nullptr;
    PollingScheduler* m_pollingScheduler = nullptr;
    ModbusSession* m_modbusSession = nullptr;
    MetricsCollector m_metricsCollector;
    DataCache m_cache;
    SqliteRepository* m_sqliteRepository = nullptr;
    QThread* m_workThread = nullptr;
    QThread* m_dbThread = nullptr;               // S8：SQLite 独立存储线程
    QTimer m_uiTimer;
    bool m_modbusConnected{false};
    bool m_isRunning{false};
    int m_telemetryCount{0};
    QVariantMap m_pendingRecord;
    // B6：帧聚合按 schema 核对 —— 记 items 名单，攒齐才推；1.5s 攒不齐强制推
    QStringList m_itemNames;
    QElapsedTimer m_recordTimer;
    // 自动停机联动：autoStop=true 的规则名集合（configure 时收集）
    QStringList m_autoStopRules;
    int m_metricsLogCounter{0};   // B11：每 60 次 uiTimer tick 打一次统计摘要
    Config m_config;
    ConfigWatcher m_configWatcher;
    TestRunner m_testRunner;

    // ── S4 ──
    bool m_anomalyMode{false};                   // 当前是否处于异常高频采样

    // ── S5 状态成员 ──
    QString m_lastServiceState{"offline"};       // serviceState 边沿检测用
    bool m_schedulerActive{false};
    qint64 m_lastSampleAgeMs{0};
    QString m_lastRuleMessage{"none"};
    QString m_lastRuleName{"none"};
    int m_triggeredRuleCount{0};
    QString m_reconnectMessage{"Disconnected"};
    QString m_startStopMessage{"idle"};
    double m_vibrationThreshold{0.0};
    double m_powerThreshold{0.0};
    double m_temperatureThreshold{0.0};
    double m_rpmThreshold{0.0};
    QString m_thresholdWriteMessage{"No threshold write yet"};
    int m_engineEvaluationCount{0};
    int m_engineRuleCount{0};
    QString m_lastEvaluationMetric{"none"};
    QStringList m_recentRuleEvents;              // 最近 4 条规则事件（40节）
};

#endif // SERVICEFACADE_H

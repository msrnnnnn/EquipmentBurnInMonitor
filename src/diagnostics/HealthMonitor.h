#ifndef HEALTHMONITOR_H
#define HEALTHMONITOR_H

#include "device/DeviceDriver.h"
#include "modbus/ModbusSession.h"
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>

class HealthMonitor : public QObject
{
    Q_OBJECT
public:
    explicit HealthMonitor(ModbusSession * session, QObject *parent = nullptr);
    void start(int intervalMs = 2000);
    void stop();
    void reset();
    bool isHealthy() const;
    // S5：暴露"距上次 good 采样"的毫秒数，供 ServiceFacade::refreshUiState 的 serviceState 判定。
    // 从未有过样本时返回 -1（语义：无采样史，区别于"很久没采样"）。
    qint64 lastSampleAgeMs() const {
        return m_lastSample.isValid() ? m_lastSample.elapsed() : -1;
    }
signals:
    void degraded();
    void recovered();
public slots:
    void onSample(const TelemetrySample &sample);
private slots:
    void check();
private:
    ModbusSession *m_session;
    QTimer m_timer;
    QElapsedTimer m_lastSample;
    bool m_wasHealthy{true};
};

#endif // HEALTHMONITOR_H

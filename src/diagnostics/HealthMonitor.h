#ifndef HEALTHMONITOR_H
#define HEALTHMONITOR_H

#include "device/DeviceDriver.h"
#include "modbus/modbussession.h"
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
    bool isHealthy() const { return m_wasHealthy; }
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

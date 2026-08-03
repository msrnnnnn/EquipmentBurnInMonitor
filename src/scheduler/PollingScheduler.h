#ifndef POLLINGSCHEDULER_H
#define POLLINGSCHEDULER_H

#include "device/DeviceDriver.h"
#include "modbus/modbussession.h"
#include <QObject>
#include <QTimer>
#include <QList>

struct PollingTask {
    DeviceDriver *driver;     // 哪个驱动
    int intervalMs;           // 轮询间隔
    qint64 lastPollMs{0};     // 上次轮询时间
};

class PollingScheduler : public QObject
{
    Q_OBJECT
public:
    explicit PollingScheduler(ModbusSession *session, QObject *parent = nullptr);
    void addTask(DeviceDriver *driver, int intervalMs);
    void start();
    void stop();
signals:
    void sampleReady(TelemetrySample sample);
private slots:
    void tick();
private:
    QList<PollingTask> m_tasks;
    ModbusSession *m_session = nullptr;
    QTimer m_timer;
};

#endif // POLLINGSCHEDULER_H

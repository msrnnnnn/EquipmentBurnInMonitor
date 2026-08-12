#ifndef POLLINGSCHEDULER_H
#define POLLINGSCHEDULER_H

#include "config/config.h"
#include "device/DeviceDriver.h"
#include "modbus/modbussession.h"
#include <QObject>
#include <QTimer>
#include <QList>

struct PollingTask {
    std::unique_ptr<DeviceDriver> driver;     // 哪个驱动
    int intervalMs;           // 轮询间隔
    qint64 lastPollMs{0};     // 上次轮询时间
};

class PollingScheduler : public QObject
{
    Q_OBJECT
public:
    explicit PollingScheduler(ModbusSession *session, QObject *parent = nullptr);
    void addTask(std::unique_ptr<DeviceDriver> driver, int intervalMs);
    void start();
    void stop();
    void tasksClear();
signals:
    void sampleReady(TelemetrySample sample);
public slots:
    void rebuildTasks(const Config &config);
private slots:
    void tick();
private:
    std::vector<PollingTask> m_tasks;
    ModbusSession *m_session = nullptr;
    QTimer m_timer;
};

#endif // POLLINGSCHEDULER_H

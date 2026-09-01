#ifndef POLLINGSCHEDULER_H
#define POLLINGSCHEDULER_H

#include "config/config.h"
#include "device/DeviceDriver.h"
#include "modbus/modbussession.h"
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QList>

struct PollingTask {
    std::unique_ptr<DeviceDriver> driver;     // 哪个驱动
    int intervalMs;           // 轮询间隔
    qint64 lastPollMs{0};     // 上次发起时间（异步化后语义从"上次完成"改为"上次发起"）
    // 该指标是否有一个请求在途。异步化后必须有：否则设备变慢时，同一指标会在
    // 响应回来前被反复发起，队列里堆满同一个指标的过期请求。
    bool pending{false};
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

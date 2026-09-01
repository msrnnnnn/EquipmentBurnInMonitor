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
    int intervalMs;           // 当前生效的轮询间隔（异常模式下会被临时改小）
    int normalIntervalMs{1000}; // 配置里的正常间隔，异常模式退出后靠它还原
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
    // 异常高频采样：on=true 把所有任务的间隔压到 m_anomalyIntervalMs，false 还原成配置值。
    // 必须由【本对象所在线程】调用（调用方用 invokeMethod 投递过来）。
    void setAnomalyMode(bool on);
private slots:
    void tick();
public:
    bool isActive() const { return m_timer.isActive(); }   // 供 ServiceFacade 的 serviceState 判定
private:
    std::vector<PollingTask> m_tasks;
    ModbusSession *m_session = nullptr;
    QTimer m_timer;
    int m_anomalyIntervalMs{100};   // 异常模式下的采样间隔（10Hz），来自 config.testProfile
    bool m_anomalyMode{false};      // 记住当前是否处于异常模式，热更新重建任务时保持一致
};

#endif // POLLINGSCHEDULER_H

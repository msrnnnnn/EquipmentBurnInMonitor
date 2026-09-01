#include "PollingScheduler.h"
#include "device/currentdevice.h"
#include "device/modbusdevicedriver.h"
#include "device/motortemperaturedevice.h"
#include "device/simpleregisterdevice.h"
#include "logging/logger.h"
#include <QDateTime>

using burninsys::Logger;

PollingScheduler::PollingScheduler(ModbusSession *session, QObject *parent)
    : QObject{parent}, m_session(session),m_timer(this)
{
    connect(&m_timer,&QTimer::timeout,this,&PollingScheduler::tick);
}

void PollingScheduler::start()
{
    if(!m_timer.isActive())
    {
        Logger::instance().info("m_timer.start");
        m_timer.start(100);
    }
}

void PollingScheduler::stop()
{
    if(m_timer.isActive()) m_timer.stop();
}

void PollingScheduler::tasksClear()
{
    m_tasks.clear();
}

void PollingScheduler::rebuildTasks(const Config &config)
{
    if (config.items.isEmpty()) {
        Logger::instance().error("no items, abort configure");
        return;
    }
    tasksClear();

    for(const PollItem &item : config.items)
    {
        std::unique_ptr<ModbusDeviceDriver> device;
        if(item.name == "temperature")  device = std::make_unique<MotorTemperatureDevice>();
        else if(item.name == "current") device = std::make_unique<CurrentDevice>();
        else                            device = std::make_unique<SimpleRegisterDevice>();
        device->setName(item.name);
        device->setUnitId(config.endpoint.unitId);
        device->setAddress(item.address);
        device->setScale(item.scale);
        addTask(std::move(device), item.intervalMs);
    }
}

void PollingScheduler::tick()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    for (PollingTask &task : m_tasks)
    {
        if (task.pending)                             continue;  // 该指标有请求在途，不重发
        if (now - task.lastPollMs < task.intervalMs)   continue;  // 还没到点

        ModbusReadRequest req = task.driver->buildReadRequest();

        // 发起即记账：语义从"上次完成"变成"上次发起"。
        // 这样设备变慢时不会疯狂补发，采样节奏稳定在 intervalMs。
        task.pending = true;
        task.lastPollMs = now;

        // 为什么用 QPointer 而不是裸指针 / 捕获 unique_ptr：
        // unique_ptr 不可捕获；而热更新（rebuildTasks）可能在响应回来之前就销毁了
        // 驱动对象。QPointer 会在对象销毁后自动置空，回调里判断一下就不会踩悬垂指针。
        QPointer<DeviceDriver> driver = task.driver.get();

        // 发起请求后【立即返回】，tick 继续处理下一个指标 —— 这就是异步化的收益：
        // 设备慢只会让某个指标的数据晚到，不会拖住整条调度循环。
        m_session->send(req, [this, driver](ModbusResponse rsp) {
            // 这个回调在【本对象线程】执行（client 用 invokeMethod 回投保证），
            // 所以可以安全访问 m_tasks，不需要加锁。
            for (PollingTask &t : m_tasks) {
                if (t.driver.get() == driver.data()) {
                    t.pending = false;   // 放行下一次采样
                    break;
                }
            }

            if (!driver)        // 驱动已被 rebuildTasks 销毁，这个结果没人要了
                return;

            TelemetrySample sample = driver->decode(rsp);
            emit sampleReady(sample);
        });
    }
}

void PollingScheduler::addTask(std::unique_ptr<DeviceDriver> driver, int intervalMs)
{
    PollingTask task;
    task.driver = std::move(driver);
    task.intervalMs = intervalMs;
    task.lastPollMs = 0;
    m_tasks.emplace_back(std::move(task));
}

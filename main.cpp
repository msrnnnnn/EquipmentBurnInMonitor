#include "mainwindow.h"
#include "equipmentdata.h"
#include "equipmentdataprovider.h"
#include "logging/logger.h"
#include "config/config.h"
#include "config/ConfigWatcher.h"

// Modbus 通信层
#include "modbus/ModbusSession.h"
#include "modbus/ModbusTcpClient.h"

// 设备驱动
#include "device/SimpleRegisterDevice.h"
#include "device/MotorTemperatureDevice.h"
#include "device/CurrentDevice.h"

// 调度器
#include "scheduler/PollingScheduler.h"

#include <QApplication>
#include <QDateTime>
#include <QRandomGenerator>
#include <QThread>
#include <QTimer>

using burninsys::Logger;

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // ── 初始化日志 ──
    Logger::instance().setLevel(Logger::Level::Info);
    Logger::instance().enableFileSink("logs/burnin.log");
    Logger::instance().info("Application started");

    // ── 加载配置 ──
    ConfigLoader loader;
    Config config = loader.loadFromFile("config.json");

    ConfigWatcher watcher;
    watcher.watch("config.json");

    if (config.endpoint.host.isEmpty() || config.items.isEmpty()) {
        Logger::instance().warn("Config load failed, using fallback");
    } else {
        Logger::instance().info("Config loaded successfully");
    }

    // ── 数据模型 ──
    EquipmentData sensor;

    // ── Modbus 通信 + 设备驱动 + 调度器（采集线程） ──
    // [面试重点] moveToThread 只搬槽函数到目标线程
    // 所以阻塞操作（session.send）必须放在 PollingScheduler 的 tick() 槽函数里
    // tick() 通过 QTimer::timeout 信号触发，在 workerThread 的事件循环里执行
    ModbusSession *session = new ModbusSession;
    PollingScheduler *scheduler = new PollingScheduler(session);

    // 创建设备驱动，从 Config 读取地址和缩放系数
    // [面试重点] 基类指针 + 多态：运行时通过 DeviceDriver* 调用不同子类的 decode
    if (!config.items.isEmpty()) {
        auto *tempDriver = new MotorTemperatureDevice;
        tempDriver->setName("temperature");
        tempDriver->setAddress(config.items[0].address);
        tempDriver->setScale(config.items[0].scale);
        tempDriver->setUnitId(config.endpoint.unitId);
        scheduler->addTask(tempDriver, config.items[0].intervalMs);

        auto *currentDriver = new CurrentDevice;
        currentDriver->setName("current");
        currentDriver->setAddress(config.items[1].address);
        currentDriver->setScale(config.items[1].scale);
        currentDriver->setUnitId(config.endpoint.unitId);
        scheduler->addTask(currentDriver, config.items[1].intervalMs);

        auto *rpmDriver = new SimpleRegisterDevice;
        rpmDriver->setName("rpm");
        rpmDriver->setAddress(config.items[2].address);
        rpmDriver->setScale(config.items[2].scale);
        rpmDriver->setUnitId(config.endpoint.unitId);
        scheduler->addTask(rpmDriver, config.items[2].intervalMs);

        auto *vibrationDriver = new SimpleRegisterDevice;
        vibrationDriver->setName("vibration");
        vibrationDriver->setAddress(config.items[3].address);
        vibrationDriver->setScale(config.items[3].scale);
        vibrationDriver->setUnitId(config.endpoint.unitId);
        scheduler->addTask(vibrationDriver, config.items[3].intervalMs);

        auto *voltageDriver = new SimpleRegisterDevice;
        voltageDriver->setName("voltage");
        voltageDriver->setAddress(config.items[4].address);
        voltageDriver->setScale(config.items[4].scale);
        voltageDriver->setUnitId(config.endpoint.unitId);
        scheduler->addTask(voltageDriver, config.items[4].intervalMs);

        auto *powerDriver = new SimpleRegisterDevice;
        powerDriver->setName("power");
        powerDriver->setAddress(config.items[5].address);
        powerDriver->setScale(config.items[5].scale);
        powerDriver->setUnitId(config.endpoint.unitId);
        scheduler->addTask(powerDriver, config.items[5].intervalMs);
    }

    // ── 移动到采集线程 ──
    // session 和 scheduler 都移到同一个 workerThread
    // session.send() 是阻塞调用，在 workerThread 执行不卡 UI
    QThread *workerThread = new QThread;
    session->moveToThread(workerThread);
    scheduler->moveToThread(workerThread);
    workerThread->start();

    // 线程启动后再开始调度（invokeMethod 在 workerThread 的事件循环里执行 start）
    QMetaObject::invokeMethod(scheduler, &PollingScheduler::start);

    // ── 采集线程 → 主线程：更新 UI ──
    // [面试重点] 跨线程信号槽：emit 在采集线程，槽函数在主线程
    // Qt 自动使用 QueuedConnection，线程安全，不需要手动加锁
    QObject::connect(scheduler, &PollingScheduler::sampleReady,
                     &sensor, [&sensor](const TelemetrySample &sample) {
        if (sample.name == "temperature")
            sensor.setTemperature(sample.value);
        else if (sample.name == "current")
            sensor.setCurrent(sample.value);
        else if (sample.name == "rpm")
            sensor.setRpm(sample.value);
        else if (sample.name == "vibration")
            sensor.setVibration(sample.value);
        else if (sample.name == "voltage")
            sensor.setVoltage(sample.value);
        else if (sample.name == "power")
            sensor.setPower(sample.value);
    });

    // ── 窗口 ──
    MainWindow w;
    w.setSensor(&sensor);
    w.show();

    return a.exec();
}

#include "mainwindow.h"
#include "equipmentdata.h"
#include "equipmentdataprovider.h"
#include "logging/logger.h"

#include <QApplication>
#include <QDateTime>
#include <QRandomGenerator>
#include <QTimer>

using namespace burninsys;

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 初始化日志
    Logger::instance().setLevel(Logger::Level::Info);
    Logger::instance().enableFileSink("logs/burnin.log");
    Logger::instance().info("Application started");

    EquipmentData sensor;

    // ── 模拟数据初始值（工业典型工况） ──
    double temperature = 45.0;
    double current     = 12.5;
    double rpm         = 1500.0;
    double vibration   = 2.3;
    double voltage     = 380.0;
    double power       = 5.5;

    // ── QTimer 每秒模拟一次采集 ──
    // [面试重点] 数据流：数据源(QTimer/Modbus) → EquipmentData(setter) → 信号 → UI刷新
    // 后面接 Modbus 时，只需替换这段QTimer，sensor.setXxx()那6行调用完全不动
    QTimer timer;
    timer.setInterval(1000);
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        auto *rng = QRandomGenerator::global();
        const auto randomDelta = [rng](double minVal, double maxVal) {
            return minVal + rng->generateDouble() * (maxVal - minVal);
        };

        // 每个指标在当前值附近小幅波动，用 qBound 限制范围
        temperature = qBound(20.0, temperature + randomDelta(-1.0, 1.0), 90.0);
        current     = qBound(0.0,  current     + randomDelta(-0.5, 0.5), 30.0);
        rpm         = qBound(0.0,  rpm         + randomDelta(-50.0, 50.0), 3000.0);
        vibration   = qBound(0.0,  vibration   + randomDelta(-0.2, 0.2), 10.0);
        voltage     = qBound(0.0,  voltage     + randomDelta(-2.0, 2.0), 450.0);
        power       = qBound(0.0,  power       + randomDelta(-0.3, 0.3), 15.0);

        // 偶尔注入异常（10%概率），用于测试异常高亮
        if (rng->generateDouble() < 0.10) {
            temperature = 85.0;
            current = 27.0;
            Logger::instance().warn(QStringLiteral("Anomaly injected: temp=%1 current=%2")
                .arg(temperature, 0, 'f', 1).arg(current, 0, 'f', 1));
        }

        // 写入数据模型 → 触发信号 → UI 刷新
        sensor.setTemperature(temperature);
        sensor.setCurrent(current);
        sensor.setRpm(rpm);
        sensor.setVibration(vibration);
        sensor.setVoltage(voltage);
        sensor.setPower(power);

        // 存历史 + 广播实时数据
        // [面试重点] QVariantMap：Qt万能容器，信号槽直接传递无需注册自定义类型
        // 统一时间轴：6个参数同一时间戳，才能做关联分析（"温度升高时电流是否也升高"）
        const bool isAnomaly = (temperature > 80.0 || current > 25.0);
        QVariantMap record;
        record["time"]        = QDateTime::currentMSecsSinceEpoch();
        record["temperature"] = temperature;
        record["current"]     = current;
        record["rpm"]         = rpm;
        record["vibration"]   = vibration;
        record["voltage"]     = voltage;
        record["power"]       = power;
        record["anomaly"]     = isAnomaly;

        sensor.provider().pushSample(record);
        sensor.provider().pushRealtime(record);
    });
    timer.start();

    MainWindow w;
    w.setSensor(&sensor);
    w.show();

    return a.exec();
}

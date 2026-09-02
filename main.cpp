#include "api/servicefacade.h"
#include "mainwindow.h"
#include "logging/logger.h"
#include "config/config.h"
#include "simulator/modbus_simulator.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>

using burninsys::Logger;

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption simulateOpt(QStringList() << "s" << "simulate", "Run with built-in Modbus simulator");
    parser.addOption(simulateOpt);
    parser.process(a);

    Logger::instance().setLevel(Logger::Level::Info);
    Logger::instance().info("Application started");

    ConfigLoader loader;
    QString base = QCoreApplication::applicationDirPath();
    Config config = loader.loadFromFile(base + "/config.json");
    Logger::instance().enableFileSink(base + "/logs/burnin.log");

    if (config.endpoint.host.isEmpty() || config.items.isEmpty()) {
        Logger::instance().warn("Config load failed, using fallback");
    } else {
        Logger::instance().info("Config loaded successfully");
    }

    // 内置模拟器（-s 或 config.simulate=true 启用，在 ServiceFacade 连接之前启动）
    // 模拟器负责：MBAP 收发 + 寄存器读写 + 随机游走（内聚）
    ModbusSimulator simulator;
    if (parser.isSet(simulateOpt) || config.simulate) {
        // 种子初始值：先给每个寄存器一个合理初值，避免启动头 1~2 秒读到 0 触发假告警
        // 对齐 config.json items[] 的 address 和 scale：
        //   temperature address=5  scale=1.0   → 25.0°C（浮点双寄存器，用 setFloatRegister 写）
        //   current     address=7  scale=0.01  → 12.00A = raw 1200
        //   rpm         address=8  scale=1.0   → 1500rpm = raw 1500
        //   vibration   address=9  scale=0.01  → 3.20mm/s = raw 320
        //   voltage     address=10 scale=0.1   → 220.0V = raw 2200
        //   power       address=11 scale=0.01  → 7.50kW = raw 750（低于 power_high 阈值 8，启动不立刻告警）
        simulator.setFloatRegister(5, 25.0f);   // 温度：25.0°C
        simulator.setHoldingRegister(7, 1200);  // 电流：12.00A
        simulator.setHoldingRegister(8, 1500);  // 转速：1500rpm
        simulator.setHoldingRegister(9, 320);   // 振动：3.20mm/s
        simulator.setHoldingRegister(10, 2200); // 电压：220.0V
        simulator.setHoldingRegister(11, 750);  // 功率：7.50kW
        simulator.start(config.endpoint.port);
        simulator.startAutoWalk({
            {5,  18.0, 35.0, 1.0, true},    // temp:  18~35°C,   ±1.0 (float 双寄存器)
            {7,  800,  1600, 300, false},   // curr:  8~16A,     ±3.0（步长 300 raw = ±3A/s：让 current_rate 规则可触发）
            {8,  1400, 1600, 100, false},   // rpm:   1400~1600, ±10
            {9,  200,  500,  50, false},    // vib:   2~5mm/s,   ±0.5
            {10, 2150, 2250, 50, false},    // volt:  215~225V,  ±5
            {11, 650,  900,  50, false}     // power: 6.5~9kW,   ±0.5（偶尔触发 power_high 展示告警）
        });
    }

    ServiceFacade facade;
    facade.start(config);
    MainWindow w;
    w.setSensor(facade.getEquipmentData());
    // S6：状态属性注入替代原来 5 条直连 —— 信号/槽的接线全部收进各页面内部，
    // main 只负责"把东西交出去"，不再关心细节。
    w.setServiceFacade(&facade);

    w.show();

    return a.exec();
}

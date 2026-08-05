#include "api/servicefacade.h"
#include "mainwindow.h"
#include "equipmentdata.h"
#include "logging/logger.h"
#include "config/config.h"
#include "config/ConfigWatcher.h"

#include <QApplication>
#include <QDateTime>
#include <QRandomGenerator>
#include <QTimer>

using burninsys::Logger;

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 初始化日志
    Logger::instance().setLevel(Logger::Level::Info);
    Logger::instance().enableFileSink("logs/burnin.log");
    Logger::instance().info("Application started");

    // 加载配置
    ConfigLoader loader;
    Config config = loader.loadFromFile("config.json");

    //监听配置文件
    ConfigWatcher watcher;
    watcher.watch("config.json");

    // 回退检查
    if (config.endpoint.host.isEmpty() || config.items.isEmpty()) {
        Logger::instance().warn("Config load failed, using fallback");
        // 用代码内置默认值
    } else {
        Logger::instance().info("Config loaded successfully");
    }

    ServiceFacade facade;
    facade.start(config);

    MainWindow w;
    w.setSensor(facade.getEquipmentData());
    w.show();

    return a.exec();
}

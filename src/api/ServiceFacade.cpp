#include "servicefacade.h"
#include "logging/logger.h"
#include "rules/ratechangerule.h"
#include "rules/thresholdrule.h"
#include <QApplication>
#include <QDir>

using burninsys::Logger;

ServiceFacade::ServiceFacade(QObject *parent)
    : QObject{parent}
{
    m_uiTimer.setInterval(1000);
    connect(&m_uiTimer, &QTimer::timeout, this, &ServiceFacade::refreshUiState);
    connect(&m_configWatcher,&ConfigWatcher::updated,this,&ServiceFacade::onConfigUpdated);
    connect(&m_equipmentData.provider(),&EquipmentDataProvider::thresholdUpdated,this,&ServiceFacade::onThresholdUpdated);
    connect(&m_equipmentData.provider(),&EquipmentDataProvider::motorCommand,this,&ServiceFacade::onMotorCommand);
    connect(&m_equipmentData.provider(),&EquipmentDataProvider::modeChanged,this,&ServiceFacade::onModeChanged);
    connect(&m_ruleEngine, &RuleEngine::ruleTriggered, this, [this](const RuleResult &result) {
        Logger::instance().warn(QStringLiteral("Rule triggered: %1 - %2").arg(result.ruleName, result.message));
        m_pendingRecord["anomaly"] = true;
    });
    connect(&m_testRunner,&TestRunner::tick,this,&ServiceFacade::remainingSecondsChanged);
    connect(&m_testRunner,&TestRunner::finished,this,&ServiceFacade::testVerdictChanged);
}

ServiceFacade::~ServiceFacade()
{
    stop();
}

void ServiceFacade::configure(const Config &config)
{
    if (config.items.isEmpty()) {
        Logger::instance().error("no items, abort configure");
        return;
    }
    rebuildRules(config);
    m_pollingScheduler->rebuildTasks(config);
}

void ServiceFacade::rebuildRules(const Config &config)
{
    m_ruleEngine.clear();
    for(const RuleConfig &rule : config.rules)
    {
        if(rule.type == "threshold")
        {
            auto ruleptr = std::make_unique<ThresholdRule>(rule.name,rule.metric,rule.threshold,rule.isUpper);
            m_ruleEngine.addRule(std::move(ruleptr));
        }
        else if(rule.type == "rate")
        {
            auto ruleptr = std::make_unique<RateChangeRule>(rule.name,rule.metric,rule.rateLimit);
            m_ruleEngine.addRule(std::move(ruleptr));
        }
    }
}

void ServiceFacade::start(const Config &config)
{
    m_config = config;
    m_configWatcher.watch(QCoreApplication::applicationDirPath() + "/config.json");
    m_workThread = new QThread(this);
    m_modbusSession = new ModbusSession;
    m_pollingScheduler = new PollingScheduler(m_modbusSession);
    m_sqliteRepository = new SqliteRepository;
    configure(config);
    m_sqliteRepository->moveToThread(m_workThread);
    m_pollingScheduler->moveToThread(m_workThread);
    m_modbusSession->moveToThread(m_workThread);
    m_workThread->start();
    QString dbPath = config.dataFilePath;
    if (QDir::isRelativePath(dbPath)) {
        dbPath = QCoreApplication::applicationDirPath() + "/" + dbPath;
    }
    QMetaObject::invokeMethod(m_sqliteRepository,[this,config,dbPath](){
        m_sqliteRepository->open(dbPath);
    });
    QMetaObject::invokeMethod(m_modbusSession,[this,config](){
        m_modbusSession->start(config.endpoint.host, config.endpoint.port);
    });
    QMetaObject::invokeMethod(m_pollingScheduler, &PollingScheduler::start);
    m_healthMonitor = new HealthMonitor(m_modbusSession,this);
    m_healthMonitor->start();
    m_testRunner.setProfile(config.testProfile);
    m_testRunner.start();
    connect(m_healthMonitor, &HealthMonitor::degraded,m_modbusSession,&ModbusSession::reconnect);
    connect(m_pollingScheduler,&PollingScheduler::sampleReady,m_healthMonitor,&HealthMonitor::onSample);
    connect(m_pollingScheduler, &PollingScheduler::sampleReady, this, &ServiceFacade::onSampleReady);
    m_uiTimer.start();
}

void ServiceFacade::stop()
{
    m_uiTimer.stop();
    m_testRunner.stop();
    if(m_healthMonitor) m_healthMonitor->stop();
    if(m_workThread && m_workThread->isRunning())
    {
        QMetaObject::invokeMethod(m_modbusSession,&ModbusSession::stop,Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(m_pollingScheduler,&PollingScheduler::stop,Qt::BlockingQueuedConnection);
        m_workThread->quit();
        m_workThread->wait();
        delete m_pollingScheduler;
        delete m_modbusSession;
        delete m_sqliteRepository;
    }
}

EquipmentData *ServiceFacade::getEquipmentData()
{
    return &m_equipmentData;
}

void ServiceFacade::onSampleReady(const TelemetrySample &sample)
{

    if (sample.name == "temperature")       m_equipmentData.setTemperature(sample.value);
    else if (sample.name == "current")      m_equipmentData.setCurrent(sample.value);
    else if (sample.name == "rpm")          m_equipmentData.setRpm(sample.value);
    else if (sample.name == "vibration")    m_equipmentData.setVibration(sample.value);
    else if (sample.name == "voltage")      m_equipmentData.setVoltage(sample.value);
    else if (sample.name == "power")        m_equipmentData.setPower(sample.value);

    m_pendingRecord["time"] = sample.timestampMs;
    m_pendingRecord[sample.name] = sample.value;
    if (sample.quality == "bad") {
        m_pendingRecord["anomaly"] = true;
    } else if (!m_pendingRecord.contains("anomaly")) {
        m_pendingRecord["anomaly"] = false;
    }


    //规则阈值判断
    if (sample.quality == "good") m_ruleEngine.evaluate(sample);


    // 6个指标都到齐了才推
    if (m_pendingRecord.size() >= 8) {  // time + 6指标 + anomaly = 8
        m_equipmentData.provider().pushSample(m_pendingRecord);
        m_pendingRecord.clear();
    }
    m_testRunner.recordSample(sample.name,sample.value);
    m_cache.put(sample);
    m_metricsCollector.record(sample);
    QMetaObject::invokeMethod(m_sqliteRepository,[=](){
        m_sqliteRepository->save(sample);
    },Qt::QueuedConnection);
    ++m_telemetryCount;
    emit telemetryCountChanged(m_telemetryCount);
}

void ServiceFacade::onThresholdUpdated(double motorTemp, double current, double rpm, double vibration, double voltage, double power)
{
    double values[] = {motorTemp, current, rpm, vibration, voltage, power};
    auto& regs = m_config.thresholdRegisters;

    for(int i = 0; i < regs.size() && i < 6; ++i)
    {
        ModbusWriteRequest req;
        req.startAddress = regs[i].address;
        req.values.append(static_cast<quint16>(values[i] / regs[i].scale));
        req.type = RegisterType::HoldingRegister;
        req.unitId = m_config.endpoint.unitId;
        // 回调式写入：不再是"只写不验" —— 写失败会留下日志，原来这种失败是静默的。
        // addr 按值捕获：回调在工作线程执行，不能跨线程访问 m_config。
        const quint16 addr = regs[i].address;
        QMetaObject::invokeMethod(m_modbusSession,[this,req,addr](){
            m_modbusSession->write(req, [addr](ModbusResponse rsp){
                if(!rsp.success)
                    Logger::instance().warn(QStringLiteral("Threshold write failed (addr=%1): %2")
                                                .arg(addr).arg(rsp.error));
            });
        },Qt::QueuedConnection);
    }
}

void ServiceFacade::onConfigUpdated(const Config &config)
{
    m_config = config;
    rebuildRules(config);
    QMetaObject::invokeMethod(m_pollingScheduler,&PollingScheduler::stop, Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_pollingScheduler,[this,config](){
        m_pollingScheduler->rebuildTasks(config);
    },Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_pollingScheduler,&PollingScheduler::start, Qt::QueuedConnection);
}

void ServiceFacade::onMotorCommand()
{
    ModbusWriteRequest req;
    req.type = RegisterType::Coil;
    req.unitId = m_config.endpoint.unitId;
    req.startAddress = m_config.motorCommand.address;
    if(!m_isRunning)
    {
        req.values.append(m_config.motorCommand.startValue);
        m_isRunning = true;
    }
    else{
        req.values.append(m_config.motorCommand.stopValue);
        m_isRunning = false;
    }
    emit motorStateChanged(m_isRunning);
    // 状态仍在本线程（GUI 线程）乐观更新；回调只负责把"写失败"暴露出来。
    QMetaObject::invokeMethod(m_modbusSession,[this,req](){
        m_modbusSession->write(req, [](ModbusResponse rsp){
            if(!rsp.success)
                Logger::instance().warn(QStringLiteral("Motor command write failed: %1").arg(rsp.error));
        });
    },Qt::QueuedConnection);
}

void ServiceFacade::onModeChanged(int mode)
{
    ModbusWriteRequest req;
    req.type = RegisterType::HoldingRegister;
    req.unitId = m_config.endpoint.unitId;
    req.startAddress = m_config.modeCommand.address;
    req.values.append(static_cast<quint16>(mode));
    QMetaObject::invokeMethod(m_modbusSession,[this,req](){
        m_modbusSession->write(req, [](ModbusResponse rsp){
            if(!rsp.success)
                Logger::instance().warn(QStringLiteral("Mode command write failed: %1").arg(rsp.error));
        });
    },Qt::QueuedConnection);
}


void ServiceFacade::refreshUiState()
{
    bool connected = m_modbusSession && m_modbusSession->isConnected();
    if (connected != m_modbusConnected) {
        m_modbusConnected = connected;
        emit modbusConnectedChanged(m_modbusConnected);
    }
    bool healthy = m_healthMonitor && m_healthMonitor->isHealthy();
    emit healthyChanged(healthy);
}

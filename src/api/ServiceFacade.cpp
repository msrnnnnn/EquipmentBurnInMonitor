#include "servicefacade.h"
#include "device/currentdevice.h"
#include "device/motortemperaturedevice.h"
#include "device/simpleregisterdevice.h"
#include "logging/logger.h"
#include "rules/ratechangerule.h"
#include "rules/thresholdrule.h"

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
    connect(&m_ruleEngine, &RuleEngine::ruleTriggered, this, [](const RuleResult &result) {
        Logger::instance().warn(QStringLiteral("Rule triggered: %1 - %2").arg(result.ruleName, result.message));
    });
}

ServiceFacade::~ServiceFacade()
{
    stop();
}

void ServiceFacade::configure(const Config &config)
{
    m_ruleEngine.clear();
    auto tempDevice = std::make_unique<MotorTemperatureDevice>();
    tempDevice->setName("temperature");
    tempDevice->setUnitId(config.endpoint.unitId);
    tempDevice->setAddress(config.items[0].address);
    tempDevice->setScale(config.items[0].scale);
    m_pollingScheduler->addTask(std::move(tempDevice), config.items[0].intervalMs);
    auto currentDevice = std::make_unique<CurrentDevice>();
    currentDevice->setName("current");
    currentDevice->setUnitId(config.endpoint.unitId);
    currentDevice->setAddress(config.items[1].address);
    currentDevice->setScale(config.items[1].scale);
    m_pollingScheduler->addTask(std::move(currentDevice), config.items[1].intervalMs);
    for(int i = 2; i < config.items.size(); ++i)
    {
        auto device = std::make_unique<SimpleRegisterDevice>();
        device->setName(config.items[i].name);
        device->setUnitId(config.endpoint.unitId);
        device->setAddress(config.items[i].address);
        device->setScale(config.items[i].scale);
        m_pollingScheduler->addTask(std::move(device), config.items[i].intervalMs);
    }
    for(const RuleConfig &rule : config.rules)
    {
        if(rule.type == "threshold")
        {
            auto ruleptr = std::make_unique<ThresholdRule>(rule.name,rule.metric,rule.threshold);
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
    m_configWatcher.watch("config.json");
    m_workThread = new QThread(this);
    m_modbusSession = new ModbusSession;
    m_pollingScheduler = new PollingScheduler(m_modbusSession);
    m_sqliteRepository = new SqliteRepository;
    configure(config);
    m_sqliteRepository->moveToThread(m_workThread);
    m_pollingScheduler->moveToThread(m_workThread);
    m_modbusSession->moveToThread(m_workThread);
    m_sqliteRepository->moveToThread(m_workThread);
    m_workThread->start();
    QMetaObject::invokeMethod(m_modbusSession,[this,config](){
        m_sqliteRepository->open(config.dataFilePath);
    });
    QMetaObject::invokeMethod(m_modbusSession,[this,config](){
        m_modbusSession->start(config.endpoint.host, config.endpoint.port);
    });
    QMetaObject::invokeMethod(m_pollingScheduler, &PollingScheduler::start);
    m_healthMonitor = new HealthMonitor(m_modbusSession,this);
    m_healthMonitor->start();
    connect(m_healthMonitor, &HealthMonitor::degraded,m_modbusSession,&ModbusSession::reconnect);
    connect(m_pollingScheduler,&PollingScheduler::sampleReady,m_healthMonitor,&HealthMonitor::onSample);
    connect(m_pollingScheduler, &PollingScheduler::sampleReady, this, &ServiceFacade::onSampleReady);
    m_uiTimer.start();
}

void ServiceFacade::stop()
{
    m_uiTimer.stop();
    if(m_workThread && m_workThread->isRunning())
    {
        QMetaObject::invokeMethod(m_modbusSession,&ModbusSession::stop,Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(m_pollingScheduler,&PollingScheduler::stop,Qt::BlockingQueuedConnection);
        m_workThread->quit();
        m_workThread->wait();
        delete m_pollingScheduler;
        delete m_modbusSession;
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

    //规则阈值判断
    m_ruleEngine.evaluate(sample);//指标异常信号触发点——ruleTriggered

    m_pendingRecord["time"] = sample.timestampMs;
    m_pendingRecord[sample.name] = sample.value;
    m_pendingRecord["anomaly"] = false;
    // 6个指标都到齐了才推
    if (m_pendingRecord.size() >= 8) {  // time + 6指标 + anomaly = 8
        m_equipmentData.provider().pushSample(m_pendingRecord);
        m_pendingRecord.clear();
    }

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
        req.values.append(static_cast<quint16>(values[i]));
        req.type = RegisterType::HoldingRegister;
        req.unitId = m_config.endpoint.unitId;
        QMetaObject::invokeMethod(m_modbusSession,[this,req](){
            m_modbusSession->write(req);
        },Qt::QueuedConnection);
    }
}

void ServiceFacade::onConfigUpdated(const Config &config)
{
    m_config = config;
    QMetaObject::invokeMethod(m_pollingScheduler,&PollingScheduler::stop);
    QMetaObject::invokeMethod(m_pollingScheduler,&PollingScheduler::tasksClear);
    configure(config);
    QMetaObject::invokeMethod(m_pollingScheduler,&PollingScheduler::start);
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
    QMetaObject::invokeMethod(m_modbusSession,[this,req](){
        m_modbusSession->write(req);
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
        m_modbusSession->write(req);
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

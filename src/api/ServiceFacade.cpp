#include "servicefacade.h"
#include "device/currentdevice.h"
#include "device/motortemperaturedevice.h"
#include "device/simpleregisterdevice.h"

ServiceFacade::ServiceFacade(QObject *parent)
    : QObject{parent}
{
    m_uiTimer.setInterval(1000);
    connect(&m_uiTimer, &QTimer::timeout, this, &ServiceFacade::refreshUiState);
}

ServiceFacade::~ServiceFacade()
{
    stop();
    qDeleteAll(m_drivers);
}

void ServiceFacade::configure(const Config &config)
{
    MotorTemperatureDevice* tempDevice = new MotorTemperatureDevice(m_pollingScheduler);
    tempDevice->setName("temperature");
    tempDevice->setUnitId(config.endpoint.unitId);
    tempDevice->setAddress(config.items[0].address);
    tempDevice->setScale(config.items[0].scale);
    m_drivers.append(tempDevice);
    CurrentDevice* currentDevice = new CurrentDevice(m_pollingScheduler);
    currentDevice->setName("current");
    currentDevice->setUnitId(config.endpoint.unitId);
    currentDevice->setAddress(config.items[1].address);
    currentDevice->setScale(config.items[1].scale);
    m_drivers.append(currentDevice);
    for(int i = 2; i < config.items.size(); ++i)
    {
        SimpleRegisterDevice* device = new SimpleRegisterDevice(m_pollingScheduler);
        device->setName(config.items[i].name);
        device->setUnitId(config.endpoint.unitId);
        device->setAddress(config.items[i].address);
        device->setScale(config.items[i].scale);
        m_drivers.append(device);
        m_pollingScheduler->addTask(device, config.items[i].intervalMs);
    }
    m_pollingScheduler->addTask(tempDevice, config.items[0].intervalMs);
    m_pollingScheduler->addTask(currentDevice, config.items[1].intervalMs);
}

void ServiceFacade::start(const Config &config)
{
    m_workThread = new QThread(this);
    m_modbusSession = new ModbusSession;
    m_pollingScheduler = new PollingScheduler(m_modbusSession);
    m_sqliteRepository = new SqliteRepository;
    m_sqliteRepository->open(config.dataFilePath);
    configure(config);
    m_pollingScheduler->moveToThread(m_workThread);
    m_modbusSession->moveToThread(m_workThread);
    m_sqliteRepository->moveToThread(m_workThread);
    m_workThread->start();
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
    if (sample.name == "temperature") m_equipmentData.setTemperature(sample.value);
    else if (sample.name == "current") m_equipmentData.setCurrent(sample.value);
    else if (sample.name == "rpm") m_equipmentData.setRpm(sample.value);
    else if (sample.name == "vibration") m_equipmentData.setVibration(sample.value);
    else if (sample.name == "voltage") m_equipmentData.setVoltage(sample.value);
    else if (sample.name == "power") m_equipmentData.setPower(sample.value);

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

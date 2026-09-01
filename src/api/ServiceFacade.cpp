#include "servicefacade.h"
#include "logging/logger.h"
#include "rules/ratechangerule.h"
#include "rules/thresholdrule.h"
#include <QApplication>
#include <QDir>
#include <QMetaObject>
#include <QTime>

using burninsys::Logger;

namespace {
// S4 异常高频采样的触发阈值（文档 8 节：温度 >80°C 或 电流 >25A）。
// 这两个值是文档规定的接近报警阈值，硬编码在这里而不是塞进 config，
// 因为它们属于"采样策略"而不是"业务参数"——真要配置化也应是独立字段。
constexpr double kAnomalyTempC   = 80.0;
constexpr double kAnomalyCurrent = 25.0;
}

ServiceFacade::ServiceFacade(QObject *parent)
    : QObject{parent}
{
    m_uiTimer.setInterval(1000);
    connect(&m_uiTimer, &QTimer::timeout, this, &ServiceFacade::refreshUiState);
    connect(&m_configWatcher,&ConfigWatcher::updated,this,&ServiceFacade::onConfigUpdated);
    connect(&m_equipmentData.provider(),&EquipmentDataProvider::thresholdUpdated,this,&ServiceFacade::onThresholdUpdated);
    connect(&m_equipmentData.provider(),&EquipmentDataProvider::motorCommand,this,&ServiceFacade::onMotorCommand);
    connect(&m_equipmentData.provider(),&EquipmentDataProvider::modeChanged,this,&ServiceFacade::onModeChanged);
    // P0-4：设置页 Connect 按钮（此前 connectRequested 无消费者，是死信号）
    connect(&m_equipmentData.provider(),&EquipmentDataProvider::connectRequested,this,&ServiceFacade::onConnectRequested);
    // S5：规则触发不只标红，还进状态体系 —— 最近规则名 / 触发计数 / 最近 4 条事件
    connect(&m_ruleEngine, &RuleEngine::ruleTriggered, this, [this](const RuleResult &result) {
        Logger::instance().warn(QStringLiteral("Rule triggered: %1 - %2").arg(result.ruleName, result.message));
        m_pendingRecord["anomaly"] = true;
        m_lastRuleMessage = result.message;
        m_lastRuleName = result.ruleName;
        ++m_triggeredRuleCount;
        m_recentRuleEvents.prepend(QStringLiteral("%1 | %2").arg(result.ruleName, result.message));
        while (m_recentRuleEvents.size() > 4) m_recentRuleEvents.removeLast();
        emit lastRuleMessageChanged();
        emit lastRuleNameChanged();
        emit triggeredRuleCountChanged();
        emit recentRuleEventsChanged();
    });
    // TestRunner 的信号带参，转发时剥掉参数 —— 属性体系统一无参 NOTIFY
    connect(&m_testRunner,&TestRunner::tick,this,[this](int){ emit remainingSecondsChanged(); });
    connect(&m_testRunner,&TestRunner::finished,this,[this](const QString&){ emit testVerdictChanged(); });
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
    // B9 修复：scheduler 可能已 moveToThread 到工作线程，不能再直接调。
    // 用 invokeMethod 以 scheduler 为上下文投递 —— 未搬家时（start() 内）直接执行，
    // 已搬家时自动变 QueuedConnection，同一段代码两条路径都安全。
    QMetaObject::invokeMethod(m_pollingScheduler, [this, config]() {
        m_pollingScheduler->rebuildTasks(config);
    });
    // S5：配置变更 = 规则体系重启，状态归零（40节）
    m_lastRuleMessage = "none";
    m_lastRuleName = "none";
    m_lastEvaluationMetric = "none";
    m_recentRuleEvents.clear();
    m_triggeredRuleCount = 0;
    m_engineEvaluationCount = 0;
    m_engineRuleCount = m_ruleEngine.ruleCount();
    emit lastRuleMessageChanged();
    emit lastRuleNameChanged();
    emit lastEvaluationMetricChanged();
    emit recentRuleEventsChanged();
    emit triggeredRuleCountChanged();
    emit engineEvaluationCountChanged();
    emit engineRuleCountChanged();
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
    // S8：采集与存储各占一个线程，磁盘 IO 慢不再拖累采集
    m_workThread = new QThread(this);
    m_dbThread  = new QThread(this);
    m_modbusSession = new ModbusSession;
    m_pollingScheduler = new PollingScheduler(m_modbusSession);
    m_sqliteRepository = new SqliteRepository;
    configure(config);   // 此时对象还没搬家，invokeMethod 直接执行
    m_sqliteRepository->moveToThread(m_dbThread);
    m_pollingScheduler->moveToThread(m_workThread);
    m_modbusSession->moveToThread(m_workThread);
    m_workThread->start();
    m_dbThread->start();
    QString dbPath = config.dataFilePath;
    if (QDir::isRelativePath(dbPath)) {
        dbPath = QCoreApplication::applicationDirPath() + "/" + dbPath;
    }
    QMetaObject::invokeMethod(m_sqliteRepository,[this,config,dbPath](){
        m_sqliteRepository->open(dbPath);
    });
    QMetaObject::invokeMethod(m_modbusSession,[this,config](){
        // P0：timeoutMs 从此真正生效 —— 之前只解析不传递，client 一直用默认 2000ms
        m_modbusSession->start(config.endpoint.host, config.endpoint.port, config.endpoint.timeoutMs);
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
    refreshUiState();   // 首帧立即刷一次，不用等 1 秒
}

void ServiceFacade::stop()
{
    m_uiTimer.stop();
    m_testRunner.stop();
    // B4 修复：healthMonitor 之前只 stop 不 delete，start 第二次会重复连接信号
    if (m_healthMonitor) {
        m_healthMonitor->stop();
        delete m_healthMonitor;
        m_healthMonitor = nullptr;
    }
    if (m_workThread && m_workThread->isRunning())
    {
        QMetaObject::invokeMethod(m_modbusSession,&ModbusSession::stop,Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(m_pollingScheduler,&PollingScheduler::stop,Qt::BlockingQueuedConnection);
        m_workThread->quit();
        m_workThread->wait();
        // S8：SQLite 连接在 db 线程创建，析构（close + removeDatabase）必须在同一线程执行，
        // 否则跨线程回收连接会出 "still in use" 类问题。阻塞投递等它真正删完。
        m_dbThread->quit();
        m_dbThread->wait();
        QMetaObject::invokeMethod(m_sqliteRepository, [this]() {
            delete m_sqliteRepository;
        }, Qt::BlockingQueuedConnection);
        delete m_pollingScheduler;
        delete m_modbusSession;
        m_pollingScheduler = nullptr;
        m_modbusSession = nullptr;
        m_sqliteRepository = nullptr;
        m_workThread->deleteLater();
        m_dbThread->deleteLater();
        m_workThread = nullptr;
        m_dbThread = nullptr;
    }
}

EquipmentData *ServiceFacade::getEquipmentData()
{
    return &m_equipmentData;
}

void ServiceFacade::onSampleReady(const TelemetrySample &sample)
{
    // A4 修复：bad 样本是"不可信"数据（解码失败时 value=0），不能把它当 0 刷进实时卡片。
    // 注意是"只挡卡片"，不是 return —— 落库 / 表格标红 / 计数这些质量标记链路必须保留
    // （README 语义：bad 数据不静默丢弃，可审计）。
    const bool good = (sample.quality == "good");

    if (good) {
        if (sample.name == "temperature")       m_equipmentData.setTemperature(sample.value);
        else if (sample.name == "current")      m_equipmentData.setCurrent(sample.value);
        else if (sample.name == "rpm")          m_equipmentData.setRpm(sample.value);
        else if (sample.name == "vibration")    m_equipmentData.setVibration(sample.value);
        else if (sample.name == "voltage")      m_equipmentData.setVoltage(sample.value);
        else if (sample.name == "power")        m_equipmentData.setPower(sample.value);
        else if (sample.name == "runStatus")    m_equipmentData.setRunStatus(static_cast<int>(sample.value));
    }

    m_pendingRecord["time"] = sample.timestampMs;
    m_pendingRecord[sample.name] = sample.value;
    if (!good) {
        m_pendingRecord["anomaly"] = true;
    } else if (!m_pendingRecord.contains("anomaly")) {
        m_pendingRecord["anomaly"] = false;
    }


    //规则阈值判断（bad 样本不进入规则，但已落库可审计）
    if (good) {
        m_ruleEngine.evaluate(sample);
        // S5：评估计数与最近评估指标（40节）
        ++m_engineEvaluationCount;
        m_lastEvaluationMetric = sample.name;
        emit engineEvaluationCountChanged();
        emit lastEvaluationMetricChanged();
    }


    // 6个指标都到齐了才推（现在含 runStatus 是 9 个 key，size>=8 仍成立）
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
    emit telemetryCountChanged();

    // S4：放在最后 —— 用本帧更新后的最新值做异常判定
    updateAnomalyMode(sample);
}

void ServiceFacade::onThresholdUpdated(double motorTemp, double current, double rpm, double vibration, double voltage, double power)
{
    // S5：快照"写前即更"（UI 立即反馈）。写结果由回调记录日志——
    // 注意回调跑在工作线程，不能跨线程改这些主线程成员，所以不做"写成功才更新"。
    setThresholdSnapshot(motorTemp, current, rpm, vibration);

    double values[] = {motorTemp, current, rpm, vibration, voltage, power};
    auto& regs = m_config.thresholdRegisters;

    for(int i = 0; i < regs.size() && i < 6; ++i)
    {
        ModbusWriteRequest req;
        req.startAddress = regs[i].address;
        req.values.append(static_cast<quint16>(values[i] / regs[i].scale));
        req.type = RegisterType::HoldingRegister;
        req.unitId = m_config.endpoint.unitId;
        // 回调式写入：写失败会留下日志，原来这种失败是静默的。
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
    Logger::instance().info("Config file changed, reloading rules & tasks");
    // S5 重构：热更新 = 重新 configure（规则重建 + 状态归零 + 任务重建），
    // 任务重建已走 invokeMethod，跨线程安全。全量重启（stop→configure→start）留待 S9。
    m_config = config;
    configure(config);
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
    // S5：启停状态进属性体系，UI 按钮样式由 isRunningChanged 驱动
    m_startStopMessage = m_isRunning ? "Motor started" : "Motor stopped";
    emit isRunningChanged();
    emit startStopMessageChanged();
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

// ── P0-4：设置页 Connect —— 换端点重启会话 ──
// 流程：先 stop 旧会话（连带停掉退避重连定时器，避免"重连到旧地址"与"连新地址"打架），
// 再用新端点 start。两个动作按顺序投递到会话线程执行。
// 期间调度器照常 tick，请求快速失败（Not connected），A4 的 quality 门保证卡片不闪 0。
void ServiceFacade::onConnectRequested(const QString &host, int port)
{
    Logger::instance().info(QStringLiteral("Manual connect requested: %1:%2").arg(host).arg(port));
    if (m_modbusSession == nullptr)
        return;
    m_config.endpoint.host = host;
    m_config.endpoint.port = port;
    const int timeoutMs = m_config.endpoint.timeoutMs;
    QMetaObject::invokeMethod(m_modbusSession, &ModbusSession::stop, Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_modbusSession, [this, host, port, timeoutMs]() {
        m_modbusSession->start(host, port, timeoutMs);
    }, Qt::QueuedConnection);
}

// ── P0-4：设置页 Test Profile 应用 ──
// 只更新规程 + 发一帧倒计时通知，不 stop/start TestRunner：
// 在跑的测试会按新时长继续（remainingSeconds 基于 m_profile 实时计算），
// 峰值历史保留 —— 改时长演示（如 72h → 2 分钟）立即在倒计时上可见。
void ServiceFacade::applyTestProfile(const TestProfile &profile)
{
    m_config.testProfile = profile;
    m_testRunner.setProfile(profile);
    if (!m_testRunner.isRunning())
        m_testRunner.start();       // 测试没在跑时（异常终止后）顺便拉起来
    emit remainingSecondsChanged(); // 不等 1s tick，立即刷 UI
}

// ── S5 属性刷新：1s 周期，全部边沿检测 ──
void ServiceFacade::refreshUiState()
{
    const bool connected = m_modbusSession && m_modbusSession->isConnected();
    if (connected != m_modbusConnected) {
        m_modbusConnected = connected;
        // 连接状态翻转时顺带更新重连消息（34节）
        m_reconnectMessage = connected ? QStringLiteral("Connected to %1:%2")
                                              .arg(m_config.endpoint.host)
                                              .arg(m_config.endpoint.port)
                                        : "Disconnected, waiting for reconnect";
        emit modbusConnectedChanged();
        emit reconnectMessageChanged();
    }

    const bool active = m_pollingScheduler && m_pollingScheduler->isActive();
    if (active != m_schedulerActive) {
        m_schedulerActive = active;
        emit schedulerActiveChanged();
    }

    const qint64 age = m_healthMonitor ? m_healthMonitor->lastSampleAgeMs() : -1;
    if (age != m_lastSampleAgeMs) {
        m_lastSampleAgeMs = age;
        emit lastSampleAgeMsChanged();
    }

    const QString state = serviceState();
    if (state != m_lastServiceState) {
        m_lastServiceState = state;
        emit serviceStateChanged();
    }
}

// ── S5 三态判定（33节）──
QString ServiceFacade::serviceState() const
{
    // age >= 0：有采样史才允许 online，避免"刚连上还没数据就亮绿"
    if (m_modbusConnected && m_schedulerActive
        && m_lastSampleAgeMs >= 0 && m_lastSampleAgeMs < 10000)
        return "online";
    if (m_modbusConnected || m_schedulerActive)
        return "degraded";
    return "offline";
}

QString ServiceFacade::endpointLabel() const
{
    return QStringLiteral("%1:%2").arg(m_config.endpoint.host).arg(m_config.endpoint.port);
}

// ── S4：异常触发高频采样 ──
void ServiceFacade::updateAnomalyMode(const TelemetrySample &sample)
{
    Q_UNUSED(sample);
    // 判定用 EquipmentData 的最新值：6 个指标不同时刻到达，取本帧更新后的快照
    const double temp = m_equipmentData.getTemperature();
    const double curr = m_equipmentData.getCurrent();
    const bool anomaly = (temp > kAnomalyTempC || curr > kAnomalyCurrent);

    if (anomaly == m_anomalyMode)
        return;   // 边沿触发：状态没翻转就不打扰调度器

    m_anomalyMode = anomaly;
    // 调度器在工作线程：跨线程切换必须投递过去，直接调用就是数据竞争。
    // QueuedConnection 保证切回调度器自己的线程执行 setAnomalyMode。
    QMetaObject::invokeMethod(m_pollingScheduler, [this, anomaly]() {
        m_pollingScheduler->setAnomalyMode(anomaly);
    }, Qt::QueuedConnection);
}

// ── S5：阈值快照（36节）──
void ServiceFacade::setThresholdSnapshot(double temp, double curr, double rpm, double vib)
{
    Q_UNUSED(curr);   // current 阈值快照未纳入属性（按文档只做 4 个）
    m_temperatureThreshold = temp;
    m_rpmThreshold = rpm;
    m_vibrationThreshold = vib;
    m_thresholdWriteMessage = QStringLiteral("Threshold registers updated at %1")
        .arg(QTime::currentTime().toString("HH:mm:ss"));
    emit thresholdsChanged();
    emit thresholdWriteMessageChanged();
}

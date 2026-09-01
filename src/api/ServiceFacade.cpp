#include "servicefacade.h"
#include "logging/logger.h"
#include "rules/ratechangerule.h"
#include "rules/thresholdrule.h"
#include <QApplication>
#include <QDir>
#include <QMetaObject>
#include <QTime>
#include <algorithm>
#include <array>
#include <memory>

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

        // 自动停机联动：命中 autoStop 规则且电机在运行 → 写停机线圈 + 状态翻转。
        // m_isRunning 守卫避免规则每秒触发时每秒都发停机写（只停一次）。
        if (m_isRunning && m_autoStopRules.contains(result.ruleName)) {
            Logger::instance().warn(QStringLiteral("Auto-stop triggered by rule: %1").arg(result.ruleName));
            m_isRunning = false;
            m_startStopMessage = QStringLiteral("Auto-stopped by %1").arg(result.ruleName);
            emit isRunningChanged();
            emit startStopMessageChanged();
            writeMotorStop();
        }
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
    // ── 规则体系（主线程对象）──
    rebuildRules(config);
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

    // B6：记录采集项名单 —— 帧聚合按 schema 核对（不再数 key 个数）
    m_itemNames.clear();
    for (const auto &item : config.items)
        m_itemNames.append(item.name);

    // 自动停机联动：收集 autoStop 规则名
    m_autoStopRules.clear();
    for (const auto &rule : config.rules)
        if (rule.autoStop)
            m_autoStopRules.append(rule.name);

    // ── 组件装配（S9：stop 后指针全空，这里幂等重建；首次 start 也走这里）──
    if (m_workThread == nullptr) m_workThread = new QThread(this);
    if (m_dbThread == nullptr)   m_dbThread  = new QThread(this);
    if (m_modbusSession == nullptr) m_modbusSession = new ModbusSession;
    if (m_pollingScheduler == nullptr) {
        m_pollingScheduler = new PollingScheduler(m_modbusSession);
        // sampleReady → 门面 的连接在此建立（scheduler 在此创建，连接随对象走）
        connect(m_pollingScheduler, &PollingScheduler::sampleReady, this, &ServiceFacade::onSampleReady);
    }
    if (m_sqliteRepository == nullptr) m_sqliteRepository = new SqliteRepository;

    // ── 线程归属（幂等：重复 moveToThread 无害；线程 start 幂等）──
    m_sqliteRepository->moveToThread(m_dbThread);
    m_pollingScheduler->moveToThread(m_workThread);
    m_modbusSession->moveToThread(m_workThread);
    if (!m_workThread->isRunning()) m_workThread->start();
    if (!m_dbThread->isRunning())   m_dbThread->start();

    // B9：任务重建必须投递到调度线程执行（对象已搬家后直接调就是跨线程）
    QMetaObject::invokeMethod(m_pollingScheduler, [this, config]() {
        m_pollingScheduler->rebuildTasks(config);
    });
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
    if (config.items.isEmpty()) {
        Logger::instance().error("start aborted: empty items");
        return;
    }
    m_config = config;
    m_configWatcher.watch(QCoreApplication::applicationDirPath() + "/config.json");
    // S9：组件创建/迁移/线程启动全部收进 configure（幂等），start 只做"启动动作"
    configure(config);

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
    if (!m_healthMonitor) {
        m_healthMonitor = new HealthMonitor(m_modbusSession,this);
        connect(m_healthMonitor, &HealthMonitor::degraded,m_modbusSession,&ModbusSession::reconnect);
        connect(m_pollingScheduler,&PollingScheduler::sampleReady,m_healthMonitor,&HealthMonitor::onSample);
    }
    m_healthMonitor->start();
    m_testRunner.setProfile(config.testProfile);
    m_testRunner.start();
    m_uiTimer.start();
    refreshUiState();   // 首帧立即刷一次，不用等 1 秒
}

void ServiceFacade::stop()
{
    m_uiTimer.stop();
    m_testRunner.abort();   // B7：人为停止判 aborted，不算 pass/fail
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
        // S8+Bug8 修复：SqliteRepository 的析构（close + removeDatabase）必须在 db 线程执行，
        // 且必须【在线程 quit 之前】以 BlockingQueuedConnection 投递 ——
        // quit 之后事件循环已退出，再投 BlockingQueued 会永久阻塞（真死锁，不是理论问题）。
        // 投递顺序保证排在队列末尾的 delete 会先处理完所有排队的 save()。
        QMetaObject::invokeMethod(m_sqliteRepository, [this]() {
            delete m_sqliteRepository;
        }, Qt::BlockingQueuedConnection);
        m_workThread->quit();
        m_workThread->wait();
        m_dbThread->quit();
        m_dbThread->wait();
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


    // B6：帧聚合改"按 schema 核对 + 超时兜底"。
    // 旧实现数 key 个数（>=8 就推）：某指标丢一轮时，上一轮旧值残留 map 里凑数，
    // 表格出现"新旧混合"的一行；items 只配 5 个时 size 永远凑不够，表格永久冻结。
    // 现在：按 config 的 items 名单逐项核对，攒齐才推；1.5s 攒不齐强制推（缺项留空）。
    if (!m_recordTimer.isValid())
        m_recordTimer.start();
    const bool complete = std::all_of(m_itemNames.cbegin(), m_itemNames.cend(),
                                      [this](const QString &n) { return m_pendingRecord.contains(n); });
    const bool timeout = m_recordTimer.elapsed() >= 1500;
    if (complete || timeout) {
        m_equipmentData.provider().pushSample(m_pendingRecord);
        m_pendingRecord.clear();
        m_recordTimer.invalidate();
    }
    // B7：bad 样本不进测试峰值（解码失败值 0 会污染 qMax 峰值，误判 FAIL）
    m_testRunner.recordSample(sample.name, sample.value, good);
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
    // P2 遗留 2 号：阈值快照改为"全部写成功才回写"。
    // 6 个写回调都跑在【工作线程】且串行执行（client 单飞），共享数组统计结果无竞争；
    // 全部收齐后 invokeMethod 回主线程更新快照 —— 跨线程改主线程成员必须走 QueuedConnection。
    const auto vals = std::array<double, 6>{motorTemp, current, rpm, vibration, voltage, power};
    auto& regs = m_config.thresholdRegisters;
    const int total = qMin(static_cast<int>(regs.size()), 6);
    if (total <= 0)
        return;

    auto results = std::make_shared<QVector<bool>>(total, false);
    auto done = std::make_shared<int>(0);

    for (int i = 0; i < total; ++i)
    {
        ModbusWriteRequest req;
        req.startAddress = regs[i].address;
        req.values.append(static_cast<quint16>(vals[i] / regs[i].scale));
        req.type = RegisterType::HoldingRegister;
        req.unitId = m_config.endpoint.unitId;
        // 全部按值捕获：回调在工作线程执行，不能跨线程碰 m_config
        const quint16 addr = regs[i].address;
        const int idx = i;
        QMetaObject::invokeMethod(m_modbusSession,[this, req, addr, idx, vals, results, done, total](){
            m_modbusSession->write(req, [this, addr, idx, vals, results, done, total](ModbusResponse rsp){
                if(!rsp.success)
                    Logger::instance().warn(QStringLiteral("Threshold write failed (addr=%1): %2")
                                                .arg(addr).arg(rsp.error));
                (*results)[idx] = rsp.success;
                if (++(*done) == total) {
                    // 全部收齐：把结论送回主线程（这里还在工作线程）
                    const bool allOk = std::all_of(results->cbegin(), results->cend(),
                                                   [](bool b) { return b; });
                    QMetaObject::invokeMethod(this, [this, allOk, vals]() {
                        if (allOk) {
                            setThresholdSnapshot(vals[0], vals[1], vals[2], vals[3], vals[4], vals[5]);
                        } else {
                            m_thresholdWriteMessage = "Threshold write failed, snapshot kept";
                            emit thresholdWriteMessageChanged();
                        }
                    }, Qt::QueuedConnection);
                }
            });
        }, Qt::QueuedConnection);
    }
}

void ServiceFacade::onConfigUpdated(const Config &config)
{
    // S9：热更新全量重启 = stop → start。
    // start() 内部会 configure（重建规则/任务/组件）+ 重新连接 + 重启 TestRunner，
    // stop() 保证旧组件/线程/信号全部回收，不会泄漏也不会重复 connect。
    // 配置合法性由 ConfigWatcher（B1）把关 —— 无效配置根本不发 updated()。
    Logger::instance().info("Config file changed, full reload (stop -> start)");
    stop();
    start(config);
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

// ── 自动停机联动：写停机线圈 ──
// 只发写请求，不翻转 m_isRunning —— 状态翻转由调用方（ruleTriggered lambda）负责，
// 保证"UI 按钮状态"与"自动停机"只有一条路径改它。
void ServiceFacade::writeMotorStop()
{
    ModbusWriteRequest req;
    req.type = RegisterType::Coil;
    req.unitId = m_config.endpoint.unitId;
    req.startAddress = m_config.motorCommand.address;
    req.values.append(m_config.motorCommand.stopValue);
    QMetaObject::invokeMethod(m_modbusSession, [this, req]() {
        m_modbusSession->write(req, [](ModbusResponse rsp) {
            if (!rsp.success)
                Logger::instance().warn(QStringLiteral("Auto-stop write failed: %1").arg(rsp.error));
        });
    }, Qt::QueuedConnection);
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

// ── B11：MetricsCollector 消费者 —— 统计摘要进日志 ──
void ServiceFacade::logMetricsSummary()
{
    const auto all = m_metricsCollector.allMetrics();
    if (all.isEmpty())
        return;
    QStringList parts;
    for (const auto &m : all) {
        parts << QStringLiteral("%1[min=%2 max=%3 avg=%4 n=%5]")
                     .arg(m.name)
                     .arg(m.min, 0, 'f', 2)
                     .arg(m.max, 0, 'f', 2)
                     .arg(m.avg, 0, 'f', 2)
                     .arg(m.count);
    }
    Logger::instance().info(QStringLiteral("Metrics summary: %1").arg(parts.join(' ')));
}

// ── S5 属性刷新：1s 周期，全部边沿检测 ──
void ServiceFacade::refreshUiState()
{
    // B11：MetricsCollector 消费者 —— 每 60 次 tick（60s）把统计摘要打进日志，
    // 让"每样本一次锁"的收集开销有真实产出（原为只写不读）。
    if (++m_metricsLogCounter >= 60) {
        m_metricsLogCounter = 0;
        logMetricsSummary();
    }

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
void ServiceFacade::setThresholdSnapshot(double temp, double curr, double rpm, double vib, double volt, double power)
{
    Q_UNUSED(curr);
    Q_UNUSED(volt);   // current/voltage 未纳入快照属性（按文档只做 4 个）
    m_temperatureThreshold = temp;
    m_rpmThreshold = rpm;
    m_vibrationThreshold = vib;
    m_powerThreshold = power;   // 修复 S5 缺口：此前漏设 power，设置页 Power 快照永远 "-"
    m_thresholdWriteMessage = QStringLiteral("Threshold registers updated at %1")
        .arg(QTime::currentTime().toString("HH:mm:ss"));
    emit thresholdsChanged();
    emit thresholdWriteMessageChanged();
}

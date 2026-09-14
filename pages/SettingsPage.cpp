#include "SettingsPage.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include "api/ServiceFacade.h"

SettingsPage::SettingsPage(QWidget *parent)
    : QWidget{parent}
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setAlignment(Qt::AlignTop);

    // ── 连接配置 ──
    auto *connGroup = new QGroupBox("Connection");
    auto *connLayout = new QFormLayout(connGroup);

    m_hostInput = new QLineEdit("127.0.0.1");
    m_hostInput->setPlaceholderText("Modbus TCP Host");
    // P0-4/A6：端口默认值与 config.json 对齐（原来 1502，改了不生效的假配置）
    m_portInput = new QLineEdit("502");
    m_portInput->setPlaceholderText("Modbus TCP Port");

    m_connectBtn = new QPushButton("Connect");

    // S6：连接/调度状态标签 —— 由 ServiceFacade 属性驱动，这里是静态初值
    m_connStatusLabel = new QLabel("● 未连接");
    m_connStatusLabel->setStyleSheet("color: #ef4444; font-weight: bold;");
    m_schedulerLabel = new QLabel("停止");
    m_schedulerLabel->setStyleSheet("color: #94a3b8;");

    connLayout->addRow("Host:", m_hostInput);
    connLayout->addRow("Port:", m_portInput);
    connLayout->addRow("Status:", m_connStatusLabel);
    connLayout->addRow("Scheduler:", m_schedulerLabel);
    connLayout->addRow("", m_connectBtn);

    // ── 报警阈值 ──
    auto *thresholdGroup = new QGroupBox("Thresholds");
    auto *thresholdLayout = new QFormLayout(thresholdGroup);

    m_tempThreshold      = new QLineEdit;  m_tempThreshold->setPlaceholderText("85.0");
    m_currentThreshold   = new QLineEdit;  m_currentThreshold->setPlaceholderText("20.0");
    m_rpmThreshold       = new QLineEdit;  m_rpmThreshold->setPlaceholderText("2000");
    m_vibrationThreshold = new QLineEdit;  m_vibrationThreshold->setPlaceholderText("10.0");
    m_voltageThreshold   = new QLineEdit;  m_voltageThreshold->setPlaceholderText("420.0");
    m_powerThreshold     = new QLineEdit;  m_powerThreshold->setPlaceholderText("8.0");

    thresholdLayout->addRow("Motor Temp (°C):", m_tempThreshold);
    thresholdLayout->addRow("Current (A):",      m_currentThreshold);
    thresholdLayout->addRow("RPM:",              m_rpmThreshold);
    thresholdLayout->addRow("Vibration (mm/s):", m_vibrationThreshold);
    thresholdLayout->addRow("Voltage (V):",      m_voltageThreshold);
    thresholdLayout->addRow("Power (kW):",       m_powerThreshold);

    m_saveBtn = new QPushButton("Save Thresholds");
    thresholdLayout->addRow("", m_saveBtn);

    // ── S6：当前生效阈值快照（36节）—— 保存后由 ServiceFacade 属性回显 ──
    auto *snapshotGroup = new QGroupBox("Active Thresholds");
    auto *snapshotLayout = new QFormLayout(snapshotGroup);
    m_tempLabel = new QLabel("-");
    m_vibrationLabel = new QLabel("-");
    m_powerLabel = new QLabel("-");
    m_rpmLabel = new QLabel("-");
    snapshotLayout->addRow("Temp (°C):", m_tempLabel);
    snapshotLayout->addRow("Vibration (mm/s):", m_vibrationLabel);
    snapshotLayout->addRow("Power (kW):", m_powerLabel);
    snapshotLayout->addRow("RPM:", m_rpmLabel);
    m_writeMessageLabel = new QLabel("No threshold write yet");
    m_writeMessageLabel->setStyleSheet("color: #94a3b8; font-size: 12px;");
    m_writeMessageLabel->setWordWrap(true);
    snapshotLayout->addRow("", m_writeMessageLabel);
    mainLayout->addWidget(snapshotGroup);

    // ── S6：最近规则消息（33节）──
    auto *ruleGroup = new QGroupBox("Rule Activity");
    auto *ruleLayout = new QFormLayout(ruleGroup);
    m_ruleLabel = new QLabel("none");
    m_ruleLabel->setStyleSheet("color: #94a3b8;");
    m_ruleLabel->setWordWrap(true);
    ruleLayout->addRow("Last rule:", m_ruleLabel);
    mainLayout->addWidget(ruleGroup);

    // ── 测试档案（P0-4：从匿名 QLineEdit 改为成员，绑定 Apply 按钮）──
    auto *profileGroup = new QGroupBox("Test Profile");
    auto *profileLayout = new QFormLayout(profileGroup);

    m_profileDuration = new QLineEdit("72");
    m_profileRpm = new QLineEdit("1500");
    m_profileLoad = new QLineEdit("5.5");
    m_profileMaxTemp = new QLineEdit("85.0");
    m_profileMaxVib = new QLineEdit("10.0");
    profileLayout->addRow("Duration (h):",      m_profileDuration);
    profileLayout->addRow("Rated RPM:",          m_profileRpm);
    profileLayout->addRow("Rated Load (kW):",    m_profileLoad);
    profileLayout->addRow("Max Temp (°C):",      m_profileMaxTemp);
    profileLayout->addRow("Max Vibration (mm/s):", m_profileMaxVib);

    // P0-4：Apply 按钮 —— 演示点：改 Duration 72 → 0.02，倒计时立刻变成 ~1 分钟
    m_applyProfileBtn = new QPushButton("Apply Profile");
    profileLayout->addRow("", m_applyProfileBtn);

    mainLayout->addWidget(connGroup);
    mainLayout->addWidget(thresholdGroup);
    mainLayout->addWidget(profileGroup);

    // ── 模式切换 ──
    auto *modeGroup = new QGroupBox("Mode");
    auto *modeLayout = new QFormLayout(modeGroup);

    m_modeCombo = new QComboBox;
    m_modeCombo->addItem("Manual", 0);
    m_modeCombo->addItem("Auto", 1);
    m_modeCombo->addItem("Test", 2);
    modeLayout->addRow("Mode:", m_modeCombo);

    mainLayout->addWidget(modeGroup);
}

void SettingsPage::bindSensor(EquipmentData *sensor)
{
    if(!sensor) return;

    connect(m_saveBtn, &QPushButton::clicked, this, [this, sensor]() {
        // B8 修复：输入校验 + 钳位。
        // 旧代码空输入 toDouble()=0 → 把温度阈值写成 0 → isUpper 规则恒真 → 告警风暴；
        // scale 无防御也会除零。现在：解析失败/<=0 拒绝发送并提示。
        bool ok = true;
        auto readVal = [&ok](QLineEdit *edit) {
            const double v = edit->text().trimmed().toDouble(&ok);
            if (!ok) return 0.0;
            return qBound(1.0, v, 100000.0);   // 阈值至少 >0，防止把规则写成恒真
        };
        const double temp = readVal(m_tempThreshold);
        const double curr = readVal(m_currentThreshold);
        const double rpm  = readVal(m_rpmThreshold);
        const double vib  = readVal(m_vibrationThreshold);
        const double volt = readVal(m_voltageThreshold);
        const double powr = readVal(m_powerThreshold);
        if (!ok) {
            m_writeMessageLabel->setText("Invalid threshold input, not sent");
            return;
        }
        sensor->provider().sendThreshold(temp, curr, rpm, vib, volt, powr);
    });

    connect(m_connectBtn, &QPushButton::clicked, this, [this, sensor]() {
        sensor->provider().sendConnectRequest(
            m_hostInput->text(),
            m_portInput->text().toInt());
    });

    connect(m_modeCombo, &QComboBox::currentIndexChanged, this, [this, sensor](int index) {
        int mode = m_modeCombo->itemData(index).toInt();
        sensor->provider().modeChanged(mode);
    });
}

// ── S6：属性订阅（与 HomePage 同款模式：NOTIFY → 读 getter → 渲染）──
void SettingsPage::bindServiceFacade(ServiceFacade *facade)
{
    if (!facade) return;

    connect(facade, &ServiceFacade::modbusConnectedChanged, this, [this, facade]() {
        const bool ok = facade->modbusConnected();
        m_connStatusLabel->setText(ok ? "● 已连接" : "● 未连接");
        m_connStatusLabel->setStyleSheet(ok ? "color: #22c55e; font-weight: bold;"
                                            : "color: #ef4444; font-weight: bold;");
    });
    connect(facade, &ServiceFacade::schedulerActiveChanged, this, [this, facade]() {
        m_schedulerLabel->setText(facade->schedulerActive() ? "运行中" : "停止");
        m_schedulerLabel->setStyleSheet(facade->schedulerActive() ? "color: #22c55e;"
                                                                  : "color: #94a3b8;");
    });
    connect(facade, &ServiceFacade::lastRuleMessageChanged, this, [this, facade]() {
        m_ruleLabel->setText(facade->lastRuleMessage());
    });
    connect(facade, &ServiceFacade::thresholdsChanged, this, [this, facade]() {
        m_tempLabel->setText(QString::number(facade->temperatureThreshold(), 'f', 1));
        m_vibrationLabel->setText(QString::number(facade->vibrationThreshold(), 'f', 1));
        m_powerLabel->setText(QString::number(facade->powerThreshold(), 'f', 1));
        m_rpmLabel->setText(QString::number(facade->rpmThreshold(), 'f', 0));
    });
    connect(facade, &ServiceFacade::thresholdWriteMessageChanged, this, [this, facade]() {
        m_writeMessageLabel->setText(facade->thresholdWriteMessage());
    });

    // P0-4：Apply Profile —— 读五个输入框构造 TestProfile，交给门面生效。
    // 演示点：Duration 改 0.02（≈72 秒）→ 首页倒计时立刻变为 ~00:01:12 并开始倒数。
    // B7：时长支持小数小时 —— >=1 走 testDurationHours，<1 折算成分钟（testDurationMinutes）。
    connect(m_applyProfileBtn, &QPushButton::clicked, this, [this, facade]() {
        TestProfile p;
        const double hours = m_profileDuration->text().toDouble();
        if (hours >= 1.0) {
            p.testDurationHours = qMax(1, static_cast<int>(hours));
            p.testDurationMinutes = 0;
        } else {
            p.testDurationHours = 0;
            p.testDurationMinutes = qMax(1, static_cast<int>(hours * 60.0 + 0.5));
        }
        p.ratedRpm = m_profileRpm->text().toInt();
        p.ratedLoad = m_profileLoad->text().toDouble();
        p.maxMotorTemp = m_profileMaxTemp->text().toDouble();
        p.maxVibration = m_profileMaxVib->text().toDouble();
        facade->applyTestProfile(p);
    });

    // 初始补帧（绑定晚于 start()，start 期间的信号已错过）
    const bool ok = facade->modbusConnected();
    m_connStatusLabel->setText(ok ? "● 已连接" : "● 未连接");
    m_connStatusLabel->setStyleSheet(ok ? "color: #22c55e; font-weight: bold;"
                                        : "color: #ef4444; font-weight: bold;");
    m_schedulerLabel->setText(facade->schedulerActive() ? "运行中" : "停止");
    m_ruleLabel->setText(facade->lastRuleMessage());
    m_tempLabel->setText(QString::number(facade->temperatureThreshold(), 'f', 1));
    m_vibrationLabel->setText(QString::number(facade->vibrationThreshold(), 'f', 1));
    m_powerLabel->setText(QString::number(facade->powerThreshold(), 'f', 1));
    m_rpmLabel->setText(QString::number(facade->rpmThreshold(), 'f', 0));
    m_writeMessageLabel->setText(facade->thresholdWriteMessage());
}

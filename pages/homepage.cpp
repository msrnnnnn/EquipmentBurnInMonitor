#include "homepage.h"
#include "api/servicefacade.h"
#include "logging/logger.h"
#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QVBoxLayout>

using burninsys::Logger;

HomePage::HomePage(QWidget *parent)
    : QWidget{parent}
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(16);

    // ── 最上层状态栏和 ──
    auto *statusBar = new QHBoxLayout;
    m_statusLabel = new QLabel("● 离线");
    m_statusLabel->setStyleSheet("color: #ef4444; font-size: 14px; font-weight: bold;");
    m_countLabel = new QLabel("采集: 0");
    m_countLabel->setStyleSheet("color: #94a3b8; font-size: 14px;");
    m_ssBtn = new QPushButton("启动");
    m_ssBtn->setStyleSheet("background-color: #2563eb; color: white; font-weight: bold; padding: 6px 16px; border-radius: 4px;");
    statusBar->addWidget(m_ssBtn);
    // P0-1：设备运行状态 —— 数据来自从站回读（模拟器把启停线圈镜像到寄存器 12），
    // 点"启动"后这里变绿"运行: 是"，是"控制闭环"当场可见的证据
    m_runLabel = new QLabel("运行: 否");
    m_runLabel->setStyleSheet("color: #94a3b8; font-size: 13px; font-weight: bold;");
    statusBar->addWidget(m_runLabel);
    statusBar->addSpacing(16);
    statusBar->addWidget(m_statusLabel);
    statusBar->addSpacing(16);
    statusBar->addWidget(m_countLabel);
    statusBar->addStretch(1);
    m_countdownLabel = new QLabel("剩余: --:--:--");
    m_countdownLabel->setStyleSheet("color: #93c5fd; font-size: 14px; font-weight: bold;");
    m_verdictLabel = new QLabel("");
    m_verdictLabel->setStyleSheet("color: #94a3b8; font-size: 14px; font-weight: bold;");
    statusBar->addWidget(m_countdownLabel);
    statusBar->addSpacing(16);
    statusBar->addWidget(m_verdictLabel);
    // S6：规则状态卡（41节简化版）—— 未触发灰色，触发后变红 ×N
    m_ruleLabel = new QLabel("规则: 等待触发");
    m_ruleLabel->setStyleSheet("color: #94a3b8; font-size: 13px; font-weight: bold;");
    statusBar->addSpacing(16);
    statusBar->addWidget(m_ruleLabel);
    root->addLayout(statusBar);

    // ── 六个指标卡片 ──
    auto *grid = new QGridLayout;
    grid->setSpacing(16);

    struct MetricDef {
        const char *name;
        QLabel **ptr;
        const char *unit;
    };

    const MetricDef metrics[] = {
        {"Motor Temperature", &m_tempLabel,      "°C"},
        {"Current",           &m_currentLabel,   "A"},
        {"RPM",               &m_rpmLabel,       "r/min"},
        {"Vibration",         &m_vibrationLabel,  "mm/s"},
        {"Voltage",           &m_voltageLabel,    "V"},
        {"Power",             &m_powerLabel,      "kW"},
    };

    const char *initialValues[] = {"-", "-", "-", "-", "-", "-"};

    for (int i = 0; i < 6; ++i) {
        auto *card = new QFrame;
        card->setFrameShape(QFrame::StyledPanel);
        card->setStyleSheet(R"(
            QFrame {
              background-color: #1e293b;
              border: 1px solid #334155;
              border-radius: 8px;
              padding: 12px;
            })");

        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setAlignment(Qt::AlignCenter);

        auto *nameLabel = new QLabel(metrics[i].name);
        nameLabel->setAlignment(Qt::AlignCenter);
        nameLabel->setStyleSheet("color: #60a5fa; font-size: 13px; border: none; background: transparent;");

        auto *valueLabel = new QLabel(QString("%1 %2").arg(initialValues[i], metrics[i].unit));
        valueLabel->setAlignment(Qt::AlignCenter);
        valueLabel->setStyleSheet("color: #ffffff; font-size: 28px; font-weight: bold; border: none; background: transparent;");
        *metrics[i].ptr = valueLabel;

        cardLayout->addWidget(nameLabel);
        cardLayout->addSpacing(8);
        cardLayout->addWidget(valueLabel);

        grid->addWidget(card, i / 3, i % 3);
    }
    root->addLayout(grid);

    // ── 统一历史表格 ──
    m_historyTable = new QTableWidget;
    m_historyTable->setColumnCount(7);
    m_historyTable->setHorizontalHeaderLabels(
        {"Time", "Motor Temp (°C)", "Current (A)", "RPM",
         "Vibration (mm/s)", "Voltage (V)", "Power (kW)"});
    m_historyTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_historyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_historyTable->verticalHeader()->setVisible(false);
    m_historyTable->setStyleSheet(R"(
        QTableWidget {
            background-color: #111827;
            color: #e2e8f0;
            gridline-color: #334155;
            border: 1px solid #334155;
            border-radius: 8px;
            font-size: 13px;
        }
        QHeaderView::section {
            background-color: #1e293b;
            color: #93c5fd;
            padding: 6px;
            border: none;
            font-weight: bold;
        }
    )");
    root->addWidget(m_historyTable, 1);
    m_chart = new QCustomPlot(this);

    // QCustomPlot 实时曲线范式：滑动窗口 + 异步重绘
    // X轴用时间戳（秒），QCPAxisTickerDateTime 自动格式化为 HH:mm:ss
    auto dateTicker = QSharedPointer<QCPAxisTickerDateTime>(new QCPAxisTickerDateTime);
    dateTicker->setDateTimeFormat("HH:mm:ss");
    m_chart->xAxis->setTicker(dateTicker);

    // 添加两条曲线：温度（左轴，红）+ 电流（右轴，蓝）—— A2 双 Y 轴量纲隔离
    m_chart->addGraph();
    m_chart->graph(0)->setPen(QPen(QColor(0xef, 0x44, 0x44), 2));
    m_chart->graph(0)->setName("Motor Temp");

    // 右轴：QCustomPlot 的 yAxis2 默认隐藏，这里显式开出来
    m_chart->addGraph(m_chart->xAxis, m_chart->yAxis2);
    m_chart->graph(1)->setPen(QPen(QColor(0x3b, 0x82, 0xf6), 2));
    m_chart->graph(1)->setName("Current");

    // Y轴固定范围 20~100°C（左轴）
    m_chart->yAxis->setRange(20, 100);
    m_chart->yAxis->setLabel("Temperature (°C)");

    // 右轴 0~30A（电流 8~16A 区间留余量）
    m_chart->yAxis2->setVisible(true);
    m_chart->yAxis2->setRange(0, 30);
    m_chart->yAxis2->setLabel("Current (A)");

    // 暗色主题配色（右轴与左轴同款）
    m_chart->setBackground(QColor(0x11, 0x18, 0x27));
    m_chart->xAxis->setLabelColor(QColor(0xcb, 0xd5, 0xe1));
    m_chart->yAxis->setLabelColor(QColor(0xcb, 0xd5, 0xe1));
    m_chart->yAxis2->setLabelColor(QColor(0xcb, 0xd5, 0xe1));
    m_chart->xAxis->setTickLabelColor(QColor(0xcb, 0xd5, 0xe1));
    m_chart->yAxis->setTickLabelColor(QColor(0xcb, 0xd5, 0xe1));
    m_chart->yAxis2->setTickLabelColor(QColor(0xcb, 0xd5, 0xe1));
    m_chart->xAxis->setBasePen(QPen(QColor(0x33, 0x41, 0x55)));
    m_chart->yAxis->setBasePen(QPen(QColor(0x33, 0x41, 0x55)));
    m_chart->yAxis2->setBasePen(QPen(QColor(0x33, 0x41, 0x55)));
    m_chart->xAxis->grid()->setPen(QPen(QColor(0x33, 0x41, 0x55)));
    m_chart->yAxis->grid()->setPen(QPen(QColor(0x33, 0x41, 0x55)));
    m_chart->yAxis2->grid()->setPen(QPen(QColor(0x33, 0x41, 0x55)));

    // X轴初始范围：当前时间前后30秒
    const double now = QDateTime::currentSecsSinceEpoch();
    m_chart->xAxis->setRange(now - 30.0, now);

    root->addWidget(m_chart, 1);
}

// 信号槽绑定：观察者模式的Qt实现
// 用lambda而非单独槽函数：6个指标逻辑相同，lambda内联更简洁
// connect的第3参数this：自动管理生命周期，HomePage销毁时自动断开连接
void HomePage::bindSensor(EquipmentData *sensor)
{
    if (!sensor) return;

    // ── 实时卡片绑定 ──
    connect(sensor, &EquipmentData::temperatureChanged, this, [this, sensor]() {
        if (m_tempLabel) {
            m_tempLabel->setText(QString::number(sensor->getTemperature(), 'f', 1) + " °C");
        }
    });
    connect(sensor, &EquipmentData::currentChanged, this, [this, sensor]() {
        if (m_currentLabel) {
            m_currentLabel->setText(QString::number(sensor->getCurrent(), 'f', 1) + " A");
        }
    });
    connect(sensor, &EquipmentData::rpmChanged, this, [this, sensor]() {
        if (m_rpmLabel) {
            m_rpmLabel->setText(QString::number(sensor->getRpm(), 'f', 0) + " r/min");
        }
    });
    connect(sensor, &EquipmentData::vibrationChanged, this, [this, sensor]() {
        if (m_vibrationLabel) {
            m_vibrationLabel->setText(QString::number(sensor->getVibration(), 'f', 1) + " mm/s");
        }
    });
    connect(sensor, &EquipmentData::voltageChanged, this, [this, sensor]() {
        if (m_voltageLabel) {
            m_voltageLabel->setText(QString::number(sensor->getVoltage(), 'f', 1) + " V");
        }
    });
    connect(sensor, &EquipmentData::powerChanged, this, [this, sensor]() {
        if (m_powerLabel) {
            m_powerLabel->setText(QString::number(sensor->getPower(), 'f', 1) + " kW");
        }
    });
    // P0-1：运行状态（从站回读 0/1）
    connect(sensor, &EquipmentData::runStatusChanged, this, [this, sensor]() {
        const bool on = sensor->getRunStatus() != 0;
        m_runLabel->setText(on ? "运行: 是" : "运行: 否");
        m_runLabel->setStyleSheet(on ? "color: #22c55e; font-size: 13px; font-weight: bold;"
                                     : "color: #94a3b8; font-size: 13px; font-weight: bold;");
    });

    // ── 实时曲线绑定 ──
    // QCustomPlot 实时追加范式：addData → removeDataBefore → 滑动X轴 → replot
    // rpQueuedReplot 异步重绘，不阻塞信号处理，高频数据时比同步replot高效
    connect(sensor, &EquipmentData::temperatureChanged, this, [this, sensor]() {
        const double now = QDateTime::currentSecsSinceEpoch();
        m_chart->graph(0)->addData(now, sensor->getTemperature());
        m_chart->graph(0)->data()->removeBefore(now - 30.0);  // 只保留最近30秒
        // A2：电流走右轴，同一时间戳追加
        m_chart->graph(1)->addData(now, sensor->getCurrent());
        m_chart->graph(1)->data()->removeBefore(now - 30.0);
        m_chart->xAxis->setRange(now - 30, now);         // X轴滑动窗口
        m_chart->replot(QCustomPlot::rpQueuedReplot);    // 异步重绘
    });

    // ── 历史表格绑定 ──
    connect(&sensor->provider(), &EquipmentDataProvider::historyUpdated,
            this, &HomePage::onHistoryUpdated);
    // ── 启停按钮切换 ──
    connect(m_ssBtn, &QPushButton::clicked, &sensor->provider(), &EquipmentDataProvider::motorCommand);
}

void HomePage::onHistoryUpdated(const QVariantList &datas)
{
    // B5：增量刷新 —— 每帧只处理新增行，旧行不动。
    // provider 的 m_history 是"尾部追加 + 满 50 裁头"：UI 里已有的行内容永不变，
    // 每帧只多 1 行（满 50 时头部被裁 1 行）。
    // 旧实现每秒全量重建 350 个 QTableWidgetItem，S4 高频采样后会变每秒 3500 次。
    const int curRows = m_historyTable->rowCount();

    if (datas.size() < curRows) {
        // 异常收缩（理论上不发生）：退回全量重建兜底
        m_historyTable->setRowCount(0);
    } else if (datas.size() == curRows) {
        // cap 稳态（50 行）：头部被裁掉一行，先删最旧
        m_historyTable->removeRow(0);
    }

    auto makeItem = [](const QString &text, const QColor &bg, const QColor &fg) {
        auto *item = new QTableWidgetItem(text);
        item->setBackground(bg);
        item->setForeground(fg);
        return item;
    };

    const int startRow = m_historyTable->rowCount();
    m_historyTable->setRowCount(datas.size());
    for (int i = startRow; i < datas.size(); ++i) {
        const auto &row = datas[i].toMap();
        const qint64 ts = row["time"].toLongLong();
        const QString timeStr = QDateTime::fromMSecsSinceEpoch(ts).toString("hh:mm:ss");
        const bool isAnomaly = row.value("anomaly", false).toBool();
        const QColor bgColor = isAnomaly ? QColor(0x7f, 0x1d, 0x1d) : QColor(0x11, 0x18, 0x27);
        const QColor fgColor = isAnomaly ? QColor(0xfe, 0xf2, 0xf2) : QColor(0xe2, 0xe8, 0xf0);

        m_historyTable->setItem(i, 0, makeItem(timeStr, bgColor, fgColor));
        m_historyTable->setItem(i, 1, makeItem(QString::number(row["temperature"].toDouble(), 'f', 1), bgColor, fgColor));
        m_historyTable->setItem(i, 2, makeItem(QString::number(row["current"].toDouble(), 'f', 1), bgColor, fgColor));
        m_historyTable->setItem(i, 3, makeItem(QString::number(row["rpm"].toDouble(), 'f', 0), bgColor, fgColor));
        m_historyTable->setItem(i, 4, makeItem(QString::number(row["vibration"].toDouble(), 'f', 1), bgColor, fgColor));
        m_historyTable->setItem(i, 5, makeItem(QString::number(row["voltage"].toDouble(), 'f', 1), bgColor, fgColor));
        m_historyTable->setItem(i, 6, makeItem(QString::number(row["power"].toDouble(), 'f', 1), bgColor, fgColor));
    }
    m_historyTable->scrollToBottom();
}

// ── S6：属性订阅 ──
// 模式：信号（无参 NOTIFY）→ lambda 读 getter → 渲染辅助函数。
// 连接晚于 facade.start()，start 期间的信号（如 configure 的规则状态归零）
// 已经错过 —— 所以末尾手动补一帧初始状态，保证 UI 不是空白。
void HomePage::bindServiceFacade(ServiceFacade *facade)
{
    if (!facade) return;

    connect(facade, &ServiceFacade::serviceStateChanged, this, [this, facade]() {
        applyServiceState(facade->serviceState());
    });
    connect(facade, &ServiceFacade::telemetryCountChanged, this, [this, facade]() {
        applyCount(facade->telemetryCount());
    });
    connect(facade, &ServiceFacade::remainingSecondsChanged, this, [this, facade]() {
        applyCountdown(facade->remainingSeconds());
    });
    connect(facade, &ServiceFacade::testVerdictChanged, this, [this, facade]() {
        applyVerdict(facade->testVerdict());
    });
    connect(facade, &ServiceFacade::isRunningChanged, this, [this, facade]() {
        applyMotorState(facade->isRunning());
    });
    connect(facade, &ServiceFacade::lastRuleNameChanged, this, [this, facade]() {
        applyRuleState(facade->lastRuleName(), facade->triggeredRuleCount());
    });

    // 初始补帧
    applyServiceState(facade->serviceState());
    applyCount(facade->telemetryCount());
    applyCountdown(facade->remainingSeconds());
    applyVerdict(facade->testVerdict());
    applyMotorState(facade->isRunning());
    applyRuleState(facade->lastRuleName(), facade->triggeredRuleCount());
}

// ── S6 渲染辅助 ──
void HomePage::applyServiceState(const QString &state)
{
    if (state == "online") {
        m_statusLabel->setText("● 在线");
        m_statusLabel->setStyleSheet("color: #22c55e; font-size: 14px; font-weight: bold;");
    } else if (state == "degraded") {
        m_statusLabel->setText("● 降级");
        m_statusLabel->setStyleSheet("color: #f59e0b; font-size: 14px; font-weight: bold;");
    } else {
        m_statusLabel->setText("● 离线");
        m_statusLabel->setStyleSheet("color: #ef4444; font-size: 14px; font-weight: bold;");
    }
}

void HomePage::applyCount(int count)
{
    m_countLabel->setText(QStringLiteral("采集: %1").arg(count));
}

void HomePage::applyCountdown(int seconds)
{
    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;
    int s = seconds % 60;
    m_countdownLabel->setText(QStringLiteral("剩余: %1:%2:%3")
        .arg(h, 2, 10, QChar('0'))
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0')));
}

void HomePage::applyVerdict(const QString &verdict)
{
    if (verdict == "pass") {
        m_verdictLabel->setText("PASS");
        m_verdictLabel->setStyleSheet("color: #22c55e; font-size: 16px; font-weight: bold;");
    } else if (verdict == "fail") {
        m_verdictLabel->setText("FAIL");
        m_verdictLabel->setStyleSheet("color: #ef4444; font-size: 16px; font-weight: bold;");
    } else if (verdict == "aborted") {
        // B7：人为中止（关机/热更新）—— 灰色 ABORTED，不是 PASS/FAIL
        m_verdictLabel->setText("ABORTED");
        m_verdictLabel->setStyleSheet("color: #94a3b8; font-size: 14px; font-weight: bold;");
    } else {
        m_verdictLabel->setText("");
    }
}

void HomePage::applyMotorState(bool running)
{
    if (running) {
        m_ssBtn->setText("停止");
        m_ssBtn->setStyleSheet("background-color: #ef4444; color: white; font-weight: bold; padding: 6px 16px; border-radius: 4px;");
    } else {
        m_ssBtn->setText("启动");
        m_ssBtn->setStyleSheet("background-color: #2563eb; color: white; font-weight: bold; padding: 6px 16px; border-radius: 4px;");
    }
}

void HomePage::applyRuleState(const QString &ruleName, int triggeredCount)
{
    if (ruleName == "none" || triggeredCount <= 0) {
        m_ruleLabel->setText("规则: 等待触发");
        m_ruleLabel->setStyleSheet("color: #94a3b8; font-size: 13px; font-weight: bold;");
    } else {
        m_ruleLabel->setText(QStringLiteral("规则: %1 ×%2").arg(ruleName).arg(triggeredCount));
        m_ruleLabel->setStyleSheet("color: #ef4444; font-size: 13px; font-weight: bold;");
    }
}

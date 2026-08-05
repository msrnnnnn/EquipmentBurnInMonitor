#include "homepage.h"

#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QVBoxLayout>

HomePage::HomePage(QWidget *parent)
    : QWidget{parent}
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(16);

    // ── 状态栏 ──
    auto *statusBar = new QHBoxLayout;
    m_statusLabel = new QLabel("● 离线");
    m_statusLabel->setStyleSheet("color: #ef4444; font-size: 14px; font-weight: bold;");
    m_countLabel = new QLabel("采集: 0");
    m_countLabel->setStyleSheet("color: #94a3b8; font-size: 14px;");
    statusBar->addWidget(m_statusLabel);
    statusBar->addSpacing(16);
    statusBar->addWidget(m_countLabel);
    statusBar->addStretch(1);
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

    // [面试重点] QCustomPlot 实时曲线范式：滑动窗口 + 异步重绘
    // X轴用时间戳（秒），QCPAxisTickerDateTime 自动格式化为 HH:mm:ss
    auto dateTicker = QSharedPointer<QCPAxisTickerDateTime>(new QCPAxisTickerDateTime);
    dateTicker->setDateTimeFormat("HH:mm:ss");
    m_chart->xAxis->setTicker(dateTicker);

    // 添加一条曲线（温度），红色，2px粗
    m_chart->addGraph();
    m_chart->graph(0)->setPen(QPen(QColor(0xef, 0x44, 0x44), 2));
    m_chart->graph(0)->setName("Motor Temp");

    // Y轴固定范围 20~100°C
    m_chart->yAxis->setRange(20, 100);
    m_chart->yAxis->setLabel("Temperature (°C)");

    // 暗色主题配色
    m_chart->setBackground(QColor(0x11, 0x18, 0x27));
    m_chart->xAxis->setLabelColor(QColor(0xcb, 0xd5, 0xe1));
    m_chart->yAxis->setLabelColor(QColor(0xcb, 0xd5, 0xe1));
    m_chart->xAxis->setTickLabelColor(QColor(0xcb, 0xd5, 0xe1));
    m_chart->yAxis->setTickLabelColor(QColor(0xcb, 0xd5, 0xe1));
    m_chart->xAxis->setBasePen(QPen(QColor(0x33, 0x41, 0x55)));
    m_chart->yAxis->setBasePen(QPen(QColor(0x33, 0x41, 0x55)));
    m_chart->xAxis->grid()->setPen(QPen(QColor(0x33, 0x41, 0x55)));
    m_chart->yAxis->grid()->setPen(QPen(QColor(0x33, 0x41, 0x55)));

    // X轴初始范围：当前时间前后30秒
    const double now = QDateTime::currentSecsSinceEpoch();
    m_chart->xAxis->setRange(now - 30.0, now);

    root->addWidget(m_chart, 1);
}

// [面试重点] 信号槽绑定：观察者模式的Qt实现
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

    // ── 实时曲线绑定 ──
    // [面试重点] QCustomPlot 实时追加范式：addData → removeDataBefore → 滑动X轴 → replot
    // rpQueuedReplot 异步重绘，不阻塞信号处理，高频数据时比同步replot高效
    connect(sensor, &EquipmentData::temperatureChanged, this, [this, sensor]() {
        const double now = QDateTime::currentSecsSinceEpoch();
        m_chart->graph(0)->addData(now, sensor->getTemperature());
        m_chart->graph(0)->data()->removeBefore(now - 30.0);  // 只保留最近30秒
        m_chart->xAxis->setRange(now - 30, now);         // X轴滑动窗口
        m_chart->replot(QCustomPlot::rpQueuedReplot);    // 异步重绘
    });

    // ── 历史表格绑定 ──
    connect(&sensor->provider(), &EquipmentDataProvider::historyUpdated,
            this, &HomePage::onHistoryUpdated);
}

void HomePage::onHistoryUpdated(const QVariantList &datas)
{
    m_historyTable->setRowCount(datas.size());
    for (int i = 0; i < datas.size(); ++i) {
        const auto &row = datas[i].toMap();
        const qint64 ts = row["time"].toLongLong();
        const QString timeStr = QDateTime::fromMSecsSinceEpoch(ts).toString("hh:mm:ss");

        const bool isAnomaly = row.value("anomaly", false).toBool();
        const QColor bgColor = isAnomaly ? QColor(0x7f, 0x1d, 0x1d) : QColor(0x11, 0x18, 0x27);
        const QColor fgColor = isAnomaly ? QColor(0xfe, 0xf2, 0xf2) : QColor(0xe2, 0xe8, 0xf0);

        auto makeItem = [&](const QString &text) {
            auto *item = new QTableWidgetItem(text);
            item->setBackground(bgColor);
            item->setForeground(fgColor);
            return item;
        };

        m_historyTable->setItem(i, 0, makeItem(timeStr));
        m_historyTable->setItem(i, 1, makeItem(QString::number(row["temperature"].toDouble(), 'f', 1)));
        m_historyTable->setItem(i, 2, makeItem(QString::number(row["current"].toDouble(), 'f', 1)));
        m_historyTable->setItem(i, 3, makeItem(QString::number(row["rpm"].toDouble(), 'f', 0)));
        m_historyTable->setItem(i, 4, makeItem(QString::number(row["vibration"].toDouble(), 'f', 1)));
        m_historyTable->setItem(i, 5, makeItem(QString::number(row["voltage"].toDouble(), 'f', 1)));
        m_historyTable->setItem(i, 6, makeItem(QString::number(row["power"].toDouble(), 'f', 1)));
    }
    m_historyTable->scrollToBottom();
}

void HomePage::updateStatus(bool healthy)
{
    if (healthy) {
        m_statusLabel->setText("● 在线");
        m_statusLabel->setStyleSheet("color: #22c55e; font-size: 14px; font-weight: bold;");
    } else {
        m_statusLabel->setText("● 异常");
        m_statusLabel->setStyleSheet("color: #ef4444; font-size: 14px; font-weight: bold;");
    }
}

void HomePage::updateCount(int count)
{
    m_countLabel->setText(QStringLiteral("采集: %1").arg(count));
}

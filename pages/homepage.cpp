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
}

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

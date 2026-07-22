#include "homepage.h"

#include <QGridLayout>
#include <QFrame>
#include <QLabel>

HomePage::HomePage(QWidget *parent)
    : QWidget{parent}
{
    auto *grid = new QGridLayout(this);
    grid->setSpacing(16);

    // 六个指标：名称 + 初始值
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

        // 指标名称
        auto *nameLabel = new QLabel(metrics[i].name);
        nameLabel->setAlignment(Qt::AlignCenter);
        nameLabel->setStyleSheet("color: #60a5fa; font-size: 13px; border: none; background: transparent;");

        // 指标数值
        auto *valueLabel = new QLabel(QString("%1 %2").arg(initialValues[i], metrics[i].unit));
        valueLabel->setAlignment(Qt::AlignCenter);
        valueLabel->setStyleSheet("color: #ffffff; font-size: 28px; font-weight: bold; border: none; background: transparent;");
        *metrics[i].ptr = valueLabel;

        cardLayout->addWidget(nameLabel);
        cardLayout->addSpacing(8);
        cardLayout->addWidget(valueLabel);

        grid->addWidget(card, i / 3, i % 3);
    }
}

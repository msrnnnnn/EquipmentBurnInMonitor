#include "settingspage.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>

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
    m_portInput = new QLineEdit("1502");
    m_portInput->setPlaceholderText("Modbus TCP Port");

    auto *connectBtn = new QPushButton("Connect");

    connLayout->addRow("Host:", m_hostInput);
    connLayout->addRow("Port:", m_portInput);
    connLayout->addRow("", connectBtn);

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

    auto *saveBtn = new QPushButton("Save Thresholds");
    thresholdLayout->addRow("", saveBtn);

    // ── 测试档案 ──
    auto *profileGroup = new QGroupBox("Test Profile");
    auto *profileLayout = new QFormLayout(profileGroup);

    profileLayout->addRow("Duration (h):",      new QLineEdit("72"));
    profileLayout->addRow("Rated RPM:",          new QLineEdit("1500"));
    profileLayout->addRow("Rated Load (kW):",    new QLineEdit("5.5"));
    profileLayout->addRow("Max Temp (°C):",      new QLineEdit("85.0"));
    profileLayout->addRow("Max Vibration (mm/s):", new QLineEdit("10.0"));

    mainLayout->addWidget(connGroup);
    mainLayout->addWidget(thresholdGroup);
    mainLayout->addWidget(profileGroup);
}

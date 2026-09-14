#ifndef SETTINGSPAGE_H
#define SETTINGSPAGE_H

#include <QWidget>
#include <QPushButton>
#include <QComboBox>
#include "EquipmentData.h"
class QLineEdit;
class QLabel;
class ServiceFacade;

class SettingsPage : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsPage(QWidget *parent = nullptr);
    void bindSensor(EquipmentData *sensor);
    // S6：连接状态 / 调度状态 / 最近规则消息 / 阈值快照 / 写消息
    void bindServiceFacade(ServiceFacade *facade);
signals:

private:
    QLineEdit *m_hostInput = nullptr;
    QLineEdit *m_portInput = nullptr;
    QLineEdit *m_tempThreshold = nullptr;
    QLineEdit *m_currentThreshold = nullptr;
    QLineEdit *m_rpmThreshold = nullptr;
    QLineEdit *m_vibrationThreshold = nullptr;
    QLineEdit *m_voltageThreshold = nullptr;
    QLineEdit *m_powerThreshold = nullptr;
    QPushButton *m_saveBtn = nullptr;
    QPushButton *m_connectBtn = nullptr;
    QComboBox *m_modeCombo = nullptr;

    // ── P0-4：Test Profile 输入（此前是匿名 QLineEdit，改成员才能绑定）──
    QLineEdit *m_profileDuration = nullptr;   // 时长（小时）
    QLineEdit *m_profileRpm = nullptr;        // 额定转速
    QLineEdit *m_profileLoad = nullptr;       // 额定负载
    QLineEdit *m_profileMaxTemp = nullptr;    // 合格阈值：最高温度
    QLineEdit *m_profileMaxVib = nullptr;     // 合格阈值：最大振动
    QPushButton *m_applyProfileBtn = nullptr;

    // ── S6 状态标签 ──
    QLabel *m_connStatusLabel = nullptr;   // 连接状态（33节）
    QLabel *m_schedulerLabel = nullptr;    // 调度器启停
    QLabel *m_ruleLabel = nullptr;         // 最近规则消息（33节）
    QLabel *m_vibrationLabel = nullptr;    // 阈值快照 ×4（36节）
    QLabel *m_powerLabel = nullptr;
    QLabel *m_tempLabel = nullptr;
    QLabel *m_rpmLabel = nullptr;
    QLabel *m_writeMessageLabel = nullptr; // 阈值写入结果
};

#endif // SETTINGSPAGE_H

#ifndef SETTINGSPAGE_H
#define SETTINGSPAGE_H

#include <QWidget>
#include <QPushButton>
#include "equipmentdata.h"
class QLineEdit;

class SettingsPage : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsPage(QWidget *parent = nullptr);
    void bindSensor(EquipmentData *sensor);
public slots:
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
};

#endif // SETTINGSPAGE_H

#ifndef SETTINGSPAGE_H
#define SETTINGSPAGE_H

#include <QWidget>

class QLineEdit;

class SettingsPage : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsPage(QWidget *parent = nullptr);

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
};

#endif // SETTINGSPAGE_H

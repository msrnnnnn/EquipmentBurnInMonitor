#ifndef HOMEPAGE_H
#define HOMEPAGE_H

#include <QWidget>

class QLabel;

class HomePage : public QWidget
{
    Q_OBJECT
public:
    explicit HomePage(QWidget *parent = nullptr);

signals:

private:
    QLabel *m_tempLabel = nullptr;
    QLabel *m_currentLabel = nullptr;
    QLabel *m_rpmLabel = nullptr;
    QLabel *m_vibrationLabel = nullptr;
    QLabel *m_voltageLabel = nullptr;
    QLabel *m_powerLabel = nullptr;
};

#endif // HOMEPAGE_H
#ifndef HOMEPAGE_H
#define HOMEPAGE_H

#include <QWidget>
#include <QPushButton>
#include <QTableWidget>
#include "equipmentdata.h"
#include "qcustomplot.h"

class QLabel;

class HomePage : public QWidget
{
    Q_OBJECT
public:
    explicit HomePage(QWidget *parent = nullptr);
    void bindSensor(EquipmentData *sensor);
    void updateStatus(bool healthy);
    void updateCount(int count);

private:
    void onHistoryUpdated(const QVariantList &datas);

    QLabel *m_tempLabel = nullptr;
    QLabel *m_currentLabel = nullptr;
    QLabel *m_rpmLabel = nullptr;
    QLabel *m_vibrationLabel = nullptr;
    QLabel *m_voltageLabel = nullptr;
    QLabel *m_powerLabel = nullptr;

    QTableWidget *m_historyTable = nullptr;
    QCustomPlot *m_chart = nullptr;
    QPushButton *m_ssBtn = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_countLabel = nullptr;
};

#endif // HOMEPAGE_H

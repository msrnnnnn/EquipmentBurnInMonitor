#ifndef HOMEPAGE_H
#define HOMEPAGE_H

#include <QWidget>
#include <QPushButton>
#include <QTableWidget>
#include "equipmentdata.h"
#include "qcustomplot.h"

class QLabel;
class ServiceFacade;

class HomePage : public QWidget
{
    Q_OBJECT
public:
    explicit HomePage(QWidget *parent = nullptr);
    void bindSensor(EquipmentData *sensor);
    // S6：订阅 ServiceFacade 的无参 NOTIFY 信号，收到后用 getter 读值渲染
    void bindServiceFacade(ServiceFacade *facade);
private:
    void onHistoryUpdated(const QVariantList &datas);
    // ── S6 渲染辅助：信号 lambda 与"初始补帧"共用，避免同一逻辑写两遍 ──
    void applyServiceState(const QString &state);
    void applyCount(int count);
    void applyCountdown(int seconds);
    void applyVerdict(const QString &verdict);
    void applyMotorState(bool running);
    void applyRuleState(const QString &ruleName, int triggeredCount);

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
    QLabel *m_countdownLabel = nullptr;
    QLabel *m_verdictLabel = nullptr;
    QLabel *m_ruleLabel = nullptr;      // S6：规则状态标签（41节简化版）
};

#endif // HOMEPAGE_H

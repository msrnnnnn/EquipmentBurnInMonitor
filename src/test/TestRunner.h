#pragma once
#include <QObject>
#include <QTimer>
#include "config/config.h"

class TestRunner : public QObject
{
    Q_OBJECT
public:
    explicit TestRunner(QObject *parent = nullptr);
    void setProfile(const TestProfile &profile);  // 设置配置
    void start();       // 重置状态，启动定时器
    void stop();        // 自然结束（到时）：算 verdict
    void abort();       // B7：人为中止（关机/热更新）：判 aborted，不算 pass/fail
    bool isRunning() const;
    int remainingSeconds() const;
    QString verdict() const;
    void recordSample(const QString &name, double value, bool good);  // B7：bad 样本不进峰值
signals:
    void tick(int remainingSeconds);        // 每秒发
    void finished(const QString &verdict);  // 测试结束发
private slots:
    void onTick();
private:
    void evaluateVerdict();

    TestProfile m_profile;
    QTimer m_timer;
    bool m_running{false};
    int m_elapsedSeconds{0};
    QString m_verdict{"pending"};
    double m_maxMotorTemp{0.0};
    double m_maxVibration{0.0};
};

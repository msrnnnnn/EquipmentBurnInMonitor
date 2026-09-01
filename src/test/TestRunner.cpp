#include "TestRunner.h"

TestRunner::TestRunner(QObject *parent) : QObject{parent}
{
    connect(&m_timer,&QTimer::timeout,this,&TestRunner::onTick);
}

void TestRunner::setProfile(const TestProfile &profile)
{
    m_profile = profile;
}

void TestRunner::start()
{
    if(!m_running)
    {
        m_elapsedSeconds = 0;
        m_maxMotorTemp = 0.0;
        m_maxVibration = 0.0;
        m_verdict = "pending";
        m_running = true;
        m_timer.start(1000);
    }
}

void TestRunner::stop()
{
    if(m_running)
    {
        if(m_timer.isActive()) m_timer.stop();
        m_running = false;
        evaluateVerdict();
        emit finished(m_verdict);
    }
}

// B7：人为中止（应用关闭 / S9 热更新）。工业语义：测试没跑完 = aborted，
// 中途停止判 PASS/FAIL 是"把没测完当合格"的谎言。
void TestRunner::abort()
{
    if(!m_running) return;
    if(m_timer.isActive()) m_timer.stop();
    m_running = false;
    m_verdict = "aborted";
    emit finished(m_verdict);
}

bool TestRunner::isRunning() const
{
    return m_running;
}

// B7：分钟级时长 —— testDurationMinutes > 0 时优先（演示 72h→1 分钟不用改结构体）
int TestRunner::remainingSeconds() const
{
    const int totalSeconds = m_profile.testDurationMinutes > 0
        ? m_profile.testDurationMinutes * 60
        : m_profile.testDurationHours * 3600;
    return totalSeconds - m_elapsedSeconds;
}

QString TestRunner::verdict() const
{
    return m_verdict;
}

// B7：bad 样本是"不可信"数据，不能进峰值统计（否则解码失败值 0 污染峰值，误判 FAIL）
void TestRunner::recordSample(const QString &name, double value, bool good)
{
    if(!good) return;
    if(name == "temperature") m_maxMotorTemp = qMax(m_maxMotorTemp, value);
    else if(name == "vibration") m_maxVibration = qMax(m_maxVibration, value);
}

void TestRunner::onTick()
{
    ++m_elapsedSeconds;
    emit tick(remainingSeconds());

    if (remainingSeconds() <= 0) stop();
}

void TestRunner::evaluateVerdict()
{
    m_verdict = (m_maxMotorTemp < m_profile.maxMotorTemp && m_maxVibration < m_profile.maxVibration) ? "pass" : "fail";
}

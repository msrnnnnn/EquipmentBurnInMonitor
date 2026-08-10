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

bool TestRunner::isRunning() const
{
    return m_running;
}

int TestRunner::remainingSeconds() const
{
    return m_profile.testDurationHours * 3600 - m_elapsedSeconds;
}

QString TestRunner::verdict() const
{
    return m_verdict;
}

void TestRunner::recordSample(const QString &name, double value)
{
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

#include "HealthMonitor.h"
#include "logging/Logger.h"
HealthMonitor::HealthMonitor(ModbusSession *session, QObject *parent)
    : QObject{parent}, m_session(session)
{
    connect(&m_timer, &QTimer::timeout, this, &HealthMonitor::check);
}

void HealthMonitor::start(int intervalMs)
{
    if(!m_timer.isActive()) m_timer.start(intervalMs);
    if(!m_lastSample.isValid()) m_lastSample.start();
}

void HealthMonitor::stop()
{
    if(m_timer.isActive()) m_timer.stop();
    if(m_lastSample.isValid())
    {
        qint64 lastDurationMs = m_lastSample.elapsed(); // 读出最后一次耗时
        burninsys::Logger::instance().info(QStringLiteral("最后一次采样间隔为: %1 ms").arg(lastDurationMs));
        m_lastSample.invalidate();
    }
}

void HealthMonitor::reset()
{
    m_lastSample.restart();
    m_wasHealthy = true;
}

bool HealthMonitor::isHealthy() const
{
    return m_wasHealthy;
}

void HealthMonitor::onSample(const TelemetrySample &sample)
{
    if(sample.quality == "good") m_lastSample.restart();
}

void HealthMonitor::check()
{
    bool connected = m_session && m_session->isConnected();
    bool healthyNow = connected && (m_lastSample.elapsed() < 10000);

    if (!healthyNow && m_wasHealthy) {
        m_wasHealthy = false;
        emit degraded();
    } else if (healthyNow && !m_wasHealthy) {
        m_wasHealthy = true;
        emit recovered();
    }
}

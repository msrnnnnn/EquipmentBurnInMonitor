#include "PollingScheduler.h"
#include <QDateTime>

PollingScheduler::PollingScheduler(ModbusSession *session, QObject *parent)
    : QObject{parent}, m_session(session)
{
    connect(&m_timer,&QTimer::timeout,this,&PollingScheduler::tick);
}

void PollingScheduler::start()
{
    if(!m_timer.isActive()) m_timer.start(100);
}

void PollingScheduler::stop()
{
    if(m_timer.isActive()) m_timer.stop();
}

void PollingScheduler::tick()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    for(PollingTask &task : m_tasks)
    {
        if(now - task.lastPollMs >= task.intervalMs)
        {
            ModbusReadRequest req = task.driver->buildReadRequest();
            ModbusResponse rsp = m_session->send(req);
            TelemetrySample sample = task.driver->decode(rsp);
            emit sampleReady(sample);
            task.lastPollMs = now;
        }
    }
}

void PollingScheduler::addTask(DeviceDriver *driver, int intervalMs)
{
    PollingTask task;
    task.driver = driver;
    task.intervalMs =  intervalMs;
    task.lastPollMs = 0;
    m_tasks.append(task);
}

#include "modbussession.h"
#include "logging/logger.h"

using burninsys::Logger;

ModbusSession::ModbusSession(QObject *parent)
    : QObject{parent}
{
    m_reconnectTimer = new QTimer(this);
    connect(m_reconnectTimer,&QTimer::timeout,this,&ModbusSession::attemptReconnect);
}

bool ModbusSession::start(const QString &host, int port)
{
    if(!m_connected)
    {
        m_host = host;
        m_port = port;
        if(m_client.open(host, port))
        {
            m_connected = true;
            emit connectionChanged(m_connected);
            return true;
        }
    }
    return false;
}

void ModbusSession::stop()
{
    if(m_reconnectTimer) m_reconnectTimer->stop();
    if(m_connected)
    {
        m_connected = false;
        emit connectionChanged(m_connected);
        m_client.close();
    }
}

ModbusResponse ModbusSession::send(const ModbusReadRequest &req)
{
    ModbusResponse rsp{};
    if(m_connected)
    {
        rsp = m_client.readRegisters(req);
        //Logger::instance().info("modbus read registers sucess");
    }else rsp.error = "Not connected";
    return rsp;
}

ModbusResponse ModbusSession::write(const ModbusWriteRequest &req)
{
    ModbusResponse rsp{};
    if(m_connected)
    {
        rsp = m_client.writeRegisters(req);

    }else rsp.error = "Not connected";
    return rsp;
}

bool ModbusSession::isConnected() const
{
    return m_connected;
}

void ModbusSession::reconnect()
{
    if(m_reconnectTimer && m_reconnectTimer->isActive()) return;
    m_connected = false;
    m_client.close();
    m_currentIntervalMs = 1000;
    m_reconnectTimer->start(m_currentIntervalMs);
    Logger::instance().warn(QStringLiteral("Reconnect triggered, first attempt in %1ms").arg(m_currentIntervalMs));
}

void ModbusSession::attemptReconnect()
{
    Logger::instance().info(QStringLiteral("Attempting reconnect to %1:%2").arg(m_host).arg(m_port));
    m_reconnectTimer->stop();
    if(m_client.open(m_host, m_port))
    {
        m_currentIntervalMs = 1000;
        m_connected = true;
        Logger::instance().info("Reconnect succeeded");
        emit connectionChanged(m_connected);
    }else{
        m_currentIntervalMs = qMin(m_currentIntervalMs*2,m_maxIntervalMs);
        m_reconnectTimer->start(m_currentIntervalMs);
        Logger::instance().warn(QStringLiteral("Reconnect failed, next attempt in %1ms").arg(m_currentIntervalMs));
    }
}

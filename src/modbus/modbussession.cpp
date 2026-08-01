#include "modbussession.h"

ModbusSession::ModbusSession(QObject *parent)
    : QObject{parent}
{}

bool ModbusSession::start(const QString &host, int port)
{
    if(!m_connected)
    {
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

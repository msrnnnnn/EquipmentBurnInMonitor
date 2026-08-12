#include "modbustcpclient.h"
#include "logging/logger.h"
#include <QVector>
#include <winsock2.h>

using burninsys::Logger;

ModbusTcpClient::ModbusTcpClient() {}

ModbusTcpClient::~ModbusTcpClient()
{
    close();
}

bool ModbusTcpClient::open(const QString &host, int port)
{
    if((m_ctx = modbus_new_tcp(host.toUtf8().constData(), port)) == nullptr)
    {
        Logger::instance().error("Modbus TCP init failed: modbus_new_tcp returned nullptr.");
        return false;
    }
    Logger::instance().info("Modbus TCP init success");
    if(modbus_connect(m_ctx) == -1){
        int savedErrno = errno;
        int wsaErr = WSAGetLastError();
        modbus_close(m_ctx);
        modbus_free(m_ctx);
        m_ctx = nullptr;
        Logger::instance().error(QStringLiteral("Modbus TCP connect failed: errno=%1 wsaErr=%2").arg(savedErrno).arg(wsaErr));
        return false;
    }
    Logger::instance().info("Modbus TCP connect success");
    m_connected = true;
    return true;
}

ModbusResponse ModbusTcpClient::readRegisters(const ModbusReadRequest &req)
{
    if(m_ctx == nullptr) return ModbusResponse{};
    QVector<uint16_t> buffer(req.quantity);
    ModbusResponse rsp;
    modbus_set_slave(m_ctx, req.unitId);
    int rc = modbus_read_registers(m_ctx, req.startAddress, req.quantity, buffer.data());
    int savedErr = errno;
    if(rc != -1){
        rsp.success = true;
        rsp.payload = QByteArray(reinterpret_cast<const char*>(buffer.data()), rc * sizeof(uint16_t));
    }
    else{
        Logger::instance().info(QStringLiteral("modbus read registers failed: %1").arg(modbus_strerror(savedErr)));
        rsp.success = false;
        rsp.error = modbus_strerror(savedErr);
        rsp.payload = QByteArray{};
    }
    return rsp;
}

ModbusResponse ModbusTcpClient::writeRegisters(const ModbusWriteRequest &req)
{
    if(m_ctx == nullptr) return ModbusResponse{};
    if(req.values.isEmpty()) return ModbusResponse{};
    ModbusResponse rsp;
    modbus_set_slave(m_ctx, req.unitId);
    int rc = -1;
    if(req.type == RegisterType::HoldingRegister)
        rc = modbus_write_registers(m_ctx,req.startAddress,req.values.size(),req.values.data());
    else if(req.type == RegisterType::Coil)
        rc = modbus_write_bit(m_ctx,req.startAddress,req.values[0]);
    int savedErr = errno;
    if(rc != -1){
        rsp.success = true;
    }
    else{
        rsp.success = false;
        rsp.error = modbus_strerror(savedErr);
        rsp.payload = QByteArray{};
    }
    return rsp;
}

void ModbusTcpClient::close()
{
    if(m_ctx)
    {
        modbus_close(m_ctx);
        modbus_free(m_ctx);
        m_connected = false;
    }
}

bool ModbusTcpClient::isConnected() const
{
    return m_connected;
}

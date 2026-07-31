#ifndef MODBUSTCPCLIENT_H
#define MODBUSTCPCLIENT_H

#include <QString>
#include "ModbusTypes.h"

extern "C" {
#include "modbus.h"   // libmodbus 的头文件，C 语言写的，需要 extern "C"
}

class ModbusTcpClient
{
public:
    ModbusTcpClient();
    ~ModbusTcpClient();
    bool open(const QString &host, int port);
    ModbusResponse readRegisters(const ModbusReadRequest& req);
    ModbusResponse writeRegisters(const ModbusWriteRequest& req);
    void close();
    bool isConnected() const;
private:
    modbus_t *m_ctx = nullptr;
    bool m_connected = false;
};

#endif // MODBUSTCPCLIENT_H

#ifndef MODBUSSESSION_H
#define MODBUSSESSION_H

#include <QObject>
#include "modbustcpclient.h"
class ModbusSession : public QObject
{
    Q_OBJECT
public:
    explicit ModbusSession(QObject *parent = nullptr);
    bool start(const QString &host, int port);
    void stop();
    ModbusResponse send(const ModbusReadRequest &req);
    ModbusResponse write(const ModbusWriteRequest &req);
    bool isConnected() const;
signals:
    void connectionChanged(bool connected);
private:
    ModbusTcpClient m_client;
    bool m_connected = false;
};

#endif // MODBUSSESSION_H

#ifndef MODBUSSESSION_H
#define MODBUSSESSION_H

#include <QObject>
#include <QTimer>
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
public slots:
    void reconnect();
private slots:
    void attemptReconnect();
private:
    ModbusTcpClient m_client;
    bool m_connected = false;
    QTimer* m_reconnectTimer = nullptr;
    QString m_host;
    int m_port{502};
    int m_currentIntervalMs{1000};
    int m_maxIntervalMs{60000};
};

#endif // MODBUSSESSION_H

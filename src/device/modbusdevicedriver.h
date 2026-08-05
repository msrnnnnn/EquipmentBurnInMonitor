#ifndef MODBUSDEVICEDRIVER_H
#define MODBUSDEVICEDRIVER_H

#include "DeviceDriver.h"

class ModbusDeviceDriver : public DeviceDriver
{
    Q_OBJECT
public:
    using DeviceDriver::DeviceDriver;
    //setter
    void setName(QString name) { m_name = name; }
    void setUnitId(int id) { m_unitId = id; }
    void setAddress(int addr) { m_address = addr; }
    void setScale(double s) { m_scale = s; }

    //getter
    QString getName() const { return m_name;}
    int getUnitId() const { return m_unitId; }
    int getAddress() const { return m_address; }
    double getScale() const { return m_scale; }


private:
    quint8 m_unitId{1};
    quint16 m_address{0};
    double m_scale{1.0};
    QString m_name{""};
};

#endif // MODBUSDEVICEDRIVER_H

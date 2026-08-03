#ifndef MODBUSDEVICEDRIVER_H
#define MODBUSDEVICEDRIVER_H

#include "DeviceDriver.h"

class ModbusDeviceDriver : public DeviceDriver
{
    Q_OBJECT
public:
    using DeviceDriver::DeviceDriver;
    //setter
    void setUnitId(int id) { m_unitId = id; }
    void setAddress(int addr) { m_address = addr; }
    void setScale(double s) { m_scale = s; }

    //getter
    int unitId() const { return m_unitId; }
    int address() const { return m_address; }
    double scale() const { return m_scale; }


private:
    quint8 m_unitId{1};
    quint16 m_address{0};
    double m_scale{1.0};
};

#endif // MODBUSDEVICEDRIVER_H

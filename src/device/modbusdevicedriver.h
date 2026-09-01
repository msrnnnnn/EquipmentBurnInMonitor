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
    // B10：寄存器类型透传（config items[].registerType，默认 holding）
    void setRegisterType(RegisterType t) { m_registerType = t; }

    //getter
    QString getName() const { return m_name;}
    int getUnitId() const { return m_unitId; }
    int getAddress() const { return m_address; }
    double getScale() const { return m_scale; }
    RegisterType getRegisterType() const { return m_registerType; }


private:
    quint8 m_unitId{1};
    quint16 m_address{0};
    double m_scale{1.0};
    QString m_name{""};
    RegisterType m_registerType{RegisterType::HoldingRegister};
};

#endif // MODBUSDEVICEDRIVER_H

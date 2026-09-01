#ifndef MOTORTEMPERATUREDEVICE_H
#define MOTORTEMPERATUREDEVICE_H

#include "modbus.h"
#include "modbusdevicedriver.h"
#include <qdatetime.h>

class MotorTemperatureDevice : public ModbusDeviceDriver
{
public:
    using ModbusDeviceDriver::ModbusDeviceDriver;

    QString name() const override
    {
        return getName();
    }

    ModbusReadRequest buildReadRequest() const override
    {
        ModbusReadRequest req;
        req.type = getRegisterType();   // B10：不再写死 holding
        req.quantity = 2;
        req.startAddress = getAddress();
        req.unitId = getUnitId();
        return req;
    }
    TelemetrySample decode(const ModbusResponse &rsp) const override
    {
        TelemetrySample sample;
        sample.name = name();
        sample.timestampMs = QDateTime::currentMSecsSinceEpoch();

        if(!rsp.success || rsp.payload.size() < 4)
        {
            sample.quality = "bad";
            return sample;
        }

        float value = modbus_get_float_abcd(reinterpret_cast<const uint16_t*>(rsp.payload.constData()));
        sample.value = static_cast<double>(value) * getScale();
        return sample;
    }
};

#endif // MOTORTEMPERATUREDEVICE_H

#ifndef MOTORTEMPERATUREDEVICE_H
#define MOTORTEMPERATUREDEVICE_H

#include "modbus.h"
#include "modbusdevicedriver.h"
#include <qdatetime.h>

class MotorTemperatureDevice : public ModbusDeviceDriver
{
public:
    using ModbusDeviceDriver::ModbusDeviceDriver;
    ModbusReadRequest buildReadRequest() const override
    {
        ModbusReadRequest req;
        req.type = RegisterType::HoldingRegister;
        req.quantity = 2;
        req.startAddress = address();
        req.unitId = unitId();
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
        sample.value = static_cast<double>(value) * scale();
        return sample;
    }
};

#endif // MOTORTEMPERATUREDEVICE_H

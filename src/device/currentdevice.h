#ifndef CURRENTDEVICE_H
#define CURRENTDEVICE_H

#include "modbusdevicedriver.h"
#include <qdatetime.h>

class CurrentDevice : public ModbusDeviceDriver
{
public:
    using ModbusDeviceDriver::ModbusDeviceDriver;
    ModbusReadRequest buildReadRequest() const override
    {
        ModbusReadRequest req;
        req.type = RegisterType::HoldingRegister;
        req.quantity = 1;
        req.startAddress = address();
        req.unitId = unitId();
        return req;
    }
    TelemetrySample decode(const ModbusResponse &rsp) const override
    {
        TelemetrySample sample;
        sample.name = name();
        sample.timestampMs = QDateTime::currentMSecsSinceEpoch();

        if(!rsp.success || rsp.payload.size() < 2)
        {
            sample.quality = "bad";
            return sample;
        }

        qint16 raw = (static_cast<quint8>(rsp.payload[0]) << 8) | static_cast<quint8>(rsp.payload[1]);
        sample.value = static_cast<double>(raw) * scale();
        return sample;
    }
};

#endif // CURRENTDEVICE_H

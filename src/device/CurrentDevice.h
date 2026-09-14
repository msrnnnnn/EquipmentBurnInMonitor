#ifndef CURRENTDEVICE_H
#define CURRENTDEVICE_H

#include "ModbusDeviceDriver.h"
#include <qdatetime.h>

class CurrentDevice : public ModbusDeviceDriver
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
        req.quantity = 1;
        req.startAddress = getAddress();
        req.unitId = getUnitId();
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

        quint16 raw;
        memcpy(&raw, rsp.payload.constData(), sizeof(quint16));
        sample.value = static_cast<double>(raw) * getScale();
        return sample;
    }
};

#endif // CURRENTDEVICE_H

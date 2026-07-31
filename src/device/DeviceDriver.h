#pragma once

#include "modbus/ModbusTypes.h"

#include <QObject>
#include <QString>

// [面试重点] 遥测采样模型：一条采样数据的完整信息
// quality 字段让每个模块自行判断如何处理坏数据，而不是在通信层直接丢弃
struct TelemetrySample
{
    QString name;            // 指标名，如 "motorTemperature"
    double value{0.0};       // 当前值
    QString quality{"good"}; // "good" 或 "bad"
    qint64 timestampMs{0};   // 毫秒时间戳
};

// [面试重点] 设备驱动抽象基类：模板方法模式
// 每种传感器实现3个纯虚函数，新增传感器只需加子类，不改现有代码
class DeviceDriver : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual ~DeviceDriver() override = default;

    virtual QString name() const = 0;
    virtual ModbusRequest buildRequest() const = 0;
    virtual TelemetrySample decode(const ModbusResponse &resp) const = 0;
};

#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

enum class RegisterType{
    Coil = 0,
    DiscreteInput,
    HoldingRegister,
    InputRegister
};


struct ModbusReadRequest{
    quint8 unitId{1};
    RegisterType type{RegisterType::HoldingRegister};
    quint16 startAddress{0};
    quint16 quantity{0};
};

struct ModbusWriteRequest{
    quint8 unitId{1};
    RegisterType type{RegisterType::HoldingRegister};
    quint16 startAddress{0};
    QVector<quint16> values;
};

struct ModbusResponse{
    bool success{false};
    QByteArray payload;
    QString error;
    // 是否为"连接级"错误（超时 / 连接被重置 / 未连接），区别于协议异常（0x01/0x02/0x03）。
    // 用途：连接级错误才触发重连与清队列；协议异常只记日志，不动连接状态。
    // 放在末尾：既有的聚合初始化 {success, payload, error} 依然有效，新字段默认 false。
    bool linkBroken{false};
};


RegisterType registerTypeFromString(const QString &s);   // 字符串 → 枚举
QString registerTypeToString(RegisterType type);          // 枚举 → 字符串


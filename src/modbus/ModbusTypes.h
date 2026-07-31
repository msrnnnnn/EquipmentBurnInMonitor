#pragma once

#include <QByteArray>
#include <QString>

enum class RegisterType{
    Coil = 0,
    DiscreteInput,
    HoldingRegister,
    InputRegister
};


struct ModbusRequest{
    quint8 unitId{1};
    RegisterType type{RegisterType::HoldingRegister};
    quint16 startAddress{0};
    quint16 quantity{0};
};


struct ModbusResponse{
    bool success{false};
    QByteArray payload;
    QString error;
};


RegisterType registerTypeFromString(const QString &s);   // 字符串 → 枚举
QString registerTypeToString(RegisterType type);          // 枚举 → 字符串


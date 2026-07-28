#include "modbus/ModbusTypes.h"

namespace burninsys {

RegisterType registerTypeFromString(const QString &s)
{
    const auto lower = s.toLower();
    if (lower == "coil") return RegisterType::Coil;
    if (lower == "discrete" || lower == "discreteinput") return RegisterType::DiscreteInput;
    if (lower == "input" || lower == "inputregister") return RegisterType::InputRegister;
    return RegisterType::HoldingRegister;  // 默认保持寄存器
}

QString registerTypeToString(RegisterType type)
{
    switch (type) {
    case RegisterType::Coil:            return "coil";
    case RegisterType::DiscreteInput:   return "discreteInput";
    case RegisterType::HoldingRegister: return "holdingRegister";
    case RegisterType::InputRegister:   return "inputRegister";
    }
    return "holdingRegister";
}

} // namespace burninsys

#include "modbus_simulator.h"
#include "logging/logger.h"
#include <QDataStream>
#include <QRandomGenerator>
#include <QTcpSocket>

using burninsys::Logger;

namespace {
// 模拟从站声明的寄存器映射上界。覆盖 config.json 用到的全部地址
//（遥测 5~12、阈值下发 100~105、模式 200）；映射之外没有寄存器，
// 访问即非法地址（0x02 异常）——从站必须拒绝，而不是静默读 0。
constexpr quint16 kRegisterMaxAddress = 255;
}

ModbusSimulator::ModbusSimulator(QObject *parent)
    : QTcpServer(parent)
{
    // 定时器每秒触发一次 autoWalkTick，让寄存器数值自己抖动，曲线才会动
    connect(&m_simTimer, &QTimer::timeout, this, &ModbusSimulator::autoWalkTick);
}

// ── 在指定端口开始监听（默认 502，Modbus 标准端口） ──
// 成功返回 true，失败返回 false（比如端口被占用）
bool ModbusSimulator::start(quint16 port)
{
    // Any 表示监听本机所有网卡，外部 127.0.0.1 也能连上
    if (!listen(QHostAddress::Any, port)) {
        Logger::instance().error(QStringLiteral("模拟器监听失败 %1: %2").arg(port).arg(errorString()));
        return false;
    }
    Logger::instance().info(QStringLiteral("模拟器已在端口 %1 监听").arg(port));
    return true;
}

// ── 停止监听并断开所有已连客户端 ──
void ModbusSimulator::stop()
{
    m_simTimer.stop(); // 先停掉随机游走，不再改寄存器
    for (QTcpSocket *c : std::as_const(m_clients)) {
        c->disconnectFromHost(); // 告诉客户端：我要断开了
        c->deleteLater();        // 延迟删除，Qt 会在事件循环下一轮安全释放
    }
    m_clients.clear();  // 清空客户端列表
    m_buffers.clear();  // 清空每个客户端的缓冲区
    close();            // 关闭监听 socket，不再接受新连接
}

// ── 预置保持寄存器初始值 ──
// address 对应 config.json 里 items[].address，value 是原始值（真实值 / scale）
void ModbusSimulator::setHoldingRegister(int address, quint16 value)
{
    m_holding[address] = value;
}

// ── 预置输入寄存器初始值（和保持寄存器分开存） ──
void ModbusSimulator::setInputRegister(int address, quint16 value)
{
    m_input[address] = value;
}

// ── 按 32 位 float 写入两个连续寄存器 ──
// 温度在 config.json 里是双寄存器浮点格式（MotorTemperatureDevice 读 2 个寄存器解 float），
// 模拟器必须用同样的字节序写，客户端才能解出正确的温度值。
void ModbusSimulator::setFloatRegister(int address, float value)
{
    writeFloatRegister(address, value);
}

// ── 配置随机游走表并启动 1 秒定时器 ──
// configs 来自 main.cpp，比如 {5, 1800, 3500, 100, false} 表示地址5在 18~35 之间每秒抖 ±1
void ModbusSimulator::startAutoWalk(const QList<AutoWalkConfig> &configs)
{
    m_autoWalkConfigs = configs;
    m_simTimer.start(1000); // 每 1000 毫秒触发一次 autoWalkTick
}

// ── 每秒执行一次：让每个配置的寄存器值随机抖一下 ──
void ModbusSimulator::autoWalkTick()
{
    for (const auto &cfg : std::as_const(m_autoWalkConfigs)) {
        if (cfg.isFloat) {
            // 浮点模式：在 float 值域内游走，写回两个寄存器（温度双寄存器）
            float cur = readFloatRegister(cfg.address);
            float delta = static_cast<float>(QRandomGenerator::global()->generateDouble() * 2.0 * cfg.stepRange - cfg.stepRange);
            float newVal = static_cast<float>(qBound(cfg.minVal, static_cast<double>(cur) + delta, cfg.maxVal));
            writeFloatRegister(cfg.address, newVal);
        } else {
            quint16 cur = m_holding.value(cfg.address, 0); // 读当前值
            int delta = QRandomGenerator::global()->bounded(-static_cast<int>(cfg.stepRange), static_cast<int>(cfg.stepRange) + 1); // 随机增量
            // 限幅，防止越界（比如温度不能跑到 100°C）
            quint16 newVal = static_cast<quint16>(qBound(cfg.minVal, static_cast<double>(cur) + delta, cfg.maxVal));
            m_holding[cfg.address] = newVal;
        }
    }
}

// ── float 双寄存器读取（与客户端 modbus_get_float_abcd 字节序对齐：高字在前、大端） ──
float ModbusSimulator::readFloatRegister(int address) const
{
    const quint16 hi = m_holding.value(address, 0);
    const quint16 lo = m_holding.value(address + 1, 0);
    const quint32 bits = (static_cast<quint32>(hi) << 16) | lo;
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

// ── float 双寄存器写入（同上字节序） ──
void ModbusSimulator::writeFloatRegister(int address, float value)
{
    quint32 bits;
    memcpy(&bits, &value, sizeof(bits));
    m_holding[address]     = static_cast<quint16>(bits >> 16);
    m_holding[address + 1] = static_cast<quint16>(bits & 0xFFFF);
}

// ── 有新客户端连上时，Qt 框架会自动回调这个函数 ──
// socketDescriptor 是内核已完成三次握手的连接 fd，Qt 替你 accept 好了
void ModbusSimulator::incomingConnection(qintptr socketDescriptor)
{
    QTcpSocket *socket = new QTcpSocket(this);
    // 把内核的 fd 绑定到 QTcpSocket 对象上，之后就能用 socket->readAll()/write() 收发
    if (!socket->setSocketDescriptor(socketDescriptor)) {
        socket->deleteLater();
        return;
    }
    // 有数据可读时，Qt 会发射 readyRead 信号
    connect(socket, &QTcpSocket::readyRead, this, &ModbusSimulator::onReadyRead);
    // 客户端断开时，Qt 会发射 disconnected 信号
    connect(socket, &QTcpSocket::disconnected, this, &ModbusSimulator::onDisconnected);
    m_clients.append(socket);  // 加入客户端列表，方便 stop() 时一起断开
    m_buffers[socket].clear(); // 为这个新客户端创建空缓冲区（解决粘包/半包）
}

// ── 某个客户端发数据来了（TCP 是流式，可能粘包/半包，需要按帧截取） ──
// 和你做 IM 时的环形缓冲区思路一样：先攒到 buffer，再按 length 字段截出完整的一帧
void ModbusSimulator::onReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    QByteArray &buf = m_buffers[socket];
    buf.append(socket->readAll()); // 追加到该客户端的缓冲区

    // 循环截帧：MBAP 最小帧 = 7 字节（头 6 + 单元标识 1 + 功能码 1）
    while (buf.size() >= 7) {
        // bytes[4..5] 是 length 字段（大端 2 字节），含义 = 从站ID(1) + 功能码(1) + 后面数据的长度
        quint16 length = static_cast<quint8>(buf[4]) << 8 | static_cast<quint8>(buf[5]);

        // length 合法域 = 从站ID(1) + PDU(≤253) → 2~254。越界说明帧界已失步，
        // 字节流上无法安全重同步，只能断开该连接（主站侧收到断开走重连链路）。
        if (length < 2 || length > 254) {
            Logger::instance().warn(QStringLiteral("Malformed frame: MBAP length=%1 out of range [2,254], dropping client")
                                        .arg(length));
            buf.clear();
            socket->disconnectFromHost();
            return;
        }

        int frameSize = 6 + length; // 6 字节 MBAP 头 + length 指示的载荷长度 = 一整帧的大小
        if (buf.size() < frameSize) return; // 半包，数据还没收齐，等下次 readyRead 再来

        QByteArray frame = buf.mid(0, frameSize); // 截出一整帧
        buf.remove(0, frameSize);                 // 从缓冲区移除已处理的部分

        QByteArray rsp = handleRequest(frame); // 交给协议解析，生成响应
        if (!rsp.isEmpty()) socket->write(rsp); // 回给客户端
    }
}

// ── 某个客户端断开了，清理记录 ──
void ModbusSimulator::onDisconnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;
    m_clients.removeAll(socket); // 从列表移除
    m_buffers.remove(socket);    // 删除该客户端的缓冲区
    socket->deleteLater();       // 延迟删除，安全
}

// ═══════════════════════════════════════════════════════════════
//  协议解析核心：MBAP 大端解析 + 功能码路由 + 封包
// ═══════════════════════════════════════════════════════════════

// ── MBAP 报文解析 + 功能码路由 ──
// 输入 adu 是一整帧 MBAP（已按 length 截好），输出是要回给客户端的 MBAP 响应
QByteArray ModbusSimulator::handleRequest(const QByteArray &adu)
{
    if (adu.size() < 12) return {};

    QDataStream ds(adu);
    ds.setByteOrder(QDataStream::BigEndian);
    quint16 transaction, protocol, length;
    quint8 unitId, function;
    quint16 start, quantity;
    ds >> transaction >> protocol >> length >> unitId >> function >> start >> quantity;

    switch (static_cast<ModbusFunction>(function)) {
    case ModbusFunction::ReadHoldingRegisters: {
        if (quantity == 0 || quantity > 125)
            return buildException(transaction, unitId, function, 0x03);
        if (start + quantity - 1 > kRegisterMaxAddress)
            return buildException(transaction, unitId, function, 0x02);
        QByteArray payload;
        QDataStream out(&payload, QIODevice::WriteOnly);
        out.setByteOrder(QDataStream::BigEndian);
        out << static_cast<quint8>(quantity * 2);
        for (int i = 0; i < quantity; ++i)
            out << m_holding.value(start + i, 0);
        return buildReadResponse(transaction, unitId, function, payload);
    }
    case ModbusFunction::ReadInputRegisters: {
        if (quantity == 0 || quantity > 125)
            return buildException(transaction, unitId, function, 0x03);
        if (start + quantity - 1 > kRegisterMaxAddress)
            return buildException(transaction, unitId, function, 0x02);
        QByteArray payload;
        QDataStream out(&payload, QIODevice::WriteOnly);
        out.setByteOrder(QDataStream::BigEndian);
        out << static_cast<quint8>(quantity * 2);
        for (int i = 0; i < quantity; ++i)
            out << m_input.value(start + i, 0);
        return buildReadResponse(transaction, unitId, function, payload);
    }
    case ModbusFunction::WriteSingleRegister: {
        // B11：变量名语义修正 —— 0x06 请求里这个字段是"要写的寄存器值"，不是数量
        const quint16 value = quantity;
        if (start > kRegisterMaxAddress)
            return buildException(transaction, unitId, function, 0x02);
        m_holding[start] = value;
        mirrorStartStop(start, value);
        return buildWriteResponse(transaction, unitId, function, start, value);
    }
    case ModbusFunction::WriteSingleCoil: {
        // 写单线圈（主站"启停"按钮用 modbus_write_bit → 0x05）
        // 规范值：0xFF00=ON / 0x0000=OFF，统一存成标准值便于读回
        const quint16 coilValue = (quantity != 0) ? 0xFF00 : 0x0000;
        if (start > kRegisterMaxAddress)   // 线圈与寄存器共用同一映射空间（模拟器简化）
            return buildException(transaction, unitId, function, 0x02);
        m_holding[start] = coilValue;
        mirrorStartStop(start, coilValue);
        // 0x05 响应 = 回显地址 + 写入值（与 0x06 同构）
        return buildWriteResponse(transaction, unitId, function, start, coilValue);
    }
    case ModbusFunction::WriteMultipleRegisters: {
        quint8 byteCount;
        ds >> byteCount;
        // 非法数据：数量为 0 或超协议上限（写多寄存器一次最多 123 个），byteCount 必须等于 quantity*2
        if (quantity == 0 || quantity > 123 || byteCount != quantity * 2)
            return buildException(transaction, unitId, function, 0x03);
        if (start + quantity - 1 > kRegisterMaxAddress)
            return buildException(transaction, unitId, function, 0x02);
        for (int i = 0; i < quantity; ++i) {
            quint16 v;
            ds >> v;
            m_holding[start + i] = v;
            mirrorStartStop(start + i, v);
        }
        return buildWriteResponse(transaction, unitId, function, start, quantity);
    }
    default:
        return buildException(transaction, unitId, function, 0x01);
    }
}

// ── 封包：读响应（0x03/0x04 用） ──
// 格式：MBAP头(7字节) + 功能码(1) + 载荷(payload)
// MBAP头 = 事务ID(2) + 协议0(2) + 长度(2) + 从站ID(1)
QByteArray ModbusSimulator::buildReadResponse(quint16 transaction, quint8 unitId, quint8 function, const QByteArray &payload)
{
    QByteArray rsp;
    QDataStream out(&rsp,QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::BigEndian);
    quint16 length = 2 + payload.size();
    out << transaction << quint16(0) << length;
    rsp.append(char(unitId));
    rsp.append(char(function));
    rsp.append(payload);
    return rsp;
}

// ── 封包：写响应（0x05/0x06/0x10 用） ──
// 格式：MBAP头 + 功能码(1) + 起始地址(2) + 数量或值(2)，length 固定 6
// 注意：所有字段统一走 QDataStream。不能"先 out << 6 字节再 rsp.append + out <<"混用——
// QDataStream 内部位置停在写入的 6 字节处，后续 out << 会覆盖 append 进去的 unitId/function。
QByteArray ModbusSimulator::buildWriteResponse(quint16 transaction, quint8 unitId, quint8 function, quint16 start, quint16 quantityOrValue)
{
    QByteArray rsp;
    QDataStream out(&rsp,QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::BigEndian);
    quint16 length = 6;
    out << transaction << quint16(0) << length;
    out << unitId << function; // quint8 各写 1 字节
    out << start << quantityOrValue;
    return rsp;
}

// ── 封包：异常响应 ──
// 格式：MBAP头 + (功能码|0x80)(1) + 异常码(1)，length=3
// 异常码：0x01=非法功能  0x02=非法地址  0x03=非法数据  0x04=从站故障
QByteArray ModbusSimulator::buildException(quint16 transaction, quint8 unitId, quint8 function, quint8 code)
{
    QByteArray rsp;
    QDataStream out(&rsp,QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::BigEndian);
    quint16 length = 3;
    out << transaction << quint16(0) << length;
    rsp.append(char(unitId));
    rsp.append(char(function | 0x80));
    rsp.append(char(code));
    return rsp;
}

// ── 联动：写地址 0 时镜像到地址 12 ──
// 意义：地址 0 是启停线圈（config.json motorCommand.address），
//       地址 12 是运行状态寄存器，点"启动"后读 12 能看到状态变化，验证控制闭环通了
// 归一化：0xFF00(ON) → 1，0x0000(OFF) → 0。
//   为什么归一化：config.json 的 runStatus 采集项用 scale=1.0 直接显示 0/1，
//   而不是把 65280 这种线圈原始值搬到界面上（那是协议层语义，不是业务语义）。
void ModbusSimulator::mirrorStartStop(int address, quint16 value)
{
    if(address == 0) m_holding[12] = (value == 0xFF00) ? 1 : 0;
}

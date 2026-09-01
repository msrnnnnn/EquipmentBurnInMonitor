#pragma once
#include <QMap>
#include <QObject>
#include <QTimer>
#include <QTcpServer>

class QTcpSocket;

enum class ModbusFunction : quint8 {
    ReadHoldingRegisters   = 0x03, // 读保持寄存器
    ReadInputRegisters     = 0x04, // 读输入寄存器
    WriteSingleCoil        = 0x05, // 写单线圈（主站"启停"按钮走这个）
    WriteSingleRegister    = 0x06, // 写单寄存器
    WriteMultipleRegisters = 0x10  // 写多寄存器 (16)
};

// 自动巡检配置：让模拟器内部自己让数值随机游走，不用 main.cpp 来管
struct AutoWalkConfig {
    int address;      // 寄存器地址，对应 config.json 里 items[].address
    double minVal;    // 值下限（int 模式：原始值 raw；float 模式：实际值，如 18.0°C）
    double maxVal;    // 值上限
    double stepRange; // 每秒随机增量范围 [-stepRange, +stepRange]
    bool isFloat{false}; // true: 按 32 位 float 读写 address 和 address+1（温度双寄存器用）
};

// Modbus 从站模拟器：假装成一台真实的 PLC/仪表
// 继承 QTcpServer 负责监听端口，每个连上来的客户端都会得到一个 QTcpSocket 通道
class ModbusSimulator : public QTcpServer
{
    Q_OBJECT
public:
    explicit ModbusSimulator(QObject *parent = nullptr);

    // 在指定端口开始监听（默认 502，Modbus 标准端口）
    bool start(quint16 port = 502);
    // 停止监听并断开所有已连客户端
    void stop();

    // 外部可预置寄存器初始值（main.cpp 用 config.json 的值来初始化）
    void setHoldingRegister(int address, quint16 value);
    void setInputRegister(int address, quint16 value);
    // 按 32 位 float 写入两个连续寄存器（温度双寄存器格式，字节序与 modbus_get_float_abcd 对齐）
    void setFloatRegister(int address, float value);

    // 启动随机游走：让 5/7/8/9/10/11 几个寄存器每秒轻微抖动，曲线才会动
    void startAutoWalk(const QList<AutoWalkConfig> &configs);

protected:
    // 有新客户端连上时，Qt 会回调这个函数
    // socketDescriptor 是内核已连好的 fd（三次握手已完成），我们只需把它绑到 QTcpSocket 上
    void incomingConnection(qintptr socketDescriptor) override;

private slots:
    void onReadyRead();    // 某个客户端发数据来了
    void onDisconnected(); // 某个客户端断开了
    void autoWalkTick();   // 每秒触发一次，让寄存器值随机游走

private:
    // 协议解析核心：收到的 MBAP 报文按功能码分发到寄存器读写
    QByteArray handleRequest(const QByteArray &adu);
    // 封包：读响应（0x03/0x04）
    QByteArray buildReadResponse(quint16 transaction, quint8 unitId, quint8 function, const QByteArray &payload);
    // 封包：写响应（0x06/0x10），固定回 start+quantity 让主站认为成功
    QByteArray buildWriteResponse(quint16 transaction, quint8 unitId, quint8 function, quint16 start, quint16 quantityOrValue);
    // 封包：异常响应，功能码 | 0x80 + 异常码
    QByteArray buildException(quint16 transaction, quint8 unitId, quint8 function, quint8 code);
    // 联动：写地址 0（启停）时镜像到地址 12（运行状态），让“启动”按钮有反馈
    void mirrorStartStop(int address, quint16 value);
    // float 双寄存器读写（温度用），字节序与客户端 modbus_get_float_abcd 一致（大端高字在前）
    float readFloatRegister(int address) const;
    void writeFloatRegister(int address, float value);

    QList<QTcpSocket*> m_clients;                  // 已连上的所有客户端通道
    QMap<QTcpSocket*, QByteArray> m_buffers;       // 每个客户端独立的接收缓冲区（解决 TCP 粘包/半包）
    QMap<int, quint16> m_holding;                  // 保持寄存器堆（地址 -> 16位值）
    QMap<int, quint16> m_input;                    // 输入寄存器堆
    QTimer m_simTimer;                             // 随机游走的定时器（1秒一跳）
    QList<AutoWalkConfig> m_autoWalkConfigs;       // 游走配置表
};

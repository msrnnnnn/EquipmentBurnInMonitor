#include "modbussession.h"
#include "logging/logger.h"

using burninsys::Logger;

ModbusSession::ModbusSession(QObject *parent)
    : QObject{parent}
    , m_client(this)   // parent 设 this：session 搬家时 client 跟着走
{
    m_reconnectTimer = new QTimer(this);
    connect(m_reconnectTimer, &QTimer::timeout, this, &ModbusSession::attemptReconnect);

    // 信号透传：client 是连接状态的事实源，session 只做二传手。
    // 这样上层（main.cpp / ServiceFacade）接的还是 session 的信号，一行不用改。
    connect(&m_client, &ModbusTcpClient::connectionChanged,
            this, &ModbusSession::connectionChanged);
}

bool ModbusSession::start(const QString &host, int port)
{
    m_host = host;
    m_port = port;
    m_client.configure(host, port);
    // 连接是阻塞操作（最长约 2s），client 内部会丢给 IO 线程 —— 这里立即返回。
    // 连接结果通过 connectionChanged 信号通知上层。
    return m_client.start();
}

void ModbusSession::stop()
{
    if (m_reconnectTimer)
        m_reconnectTimer->stop();
    m_client.stop();   // 阻塞投递 doDisconnect + IO 线程退出（关闭时唯一一次等待）
}

void ModbusSession::send(const ModbusReadRequest &req,
                         std::function<void(ModbusResponse)> cb)
{
    // 直接转交 client：未连接的快速失败、单飞排队、回调回本线程，全由 client 负责
    m_client.readRegisters(req, std::move(cb));
}

void ModbusSession::write(const ModbusWriteRequest &req,
                          std::function<void(ModbusResponse)> cb)
{
    m_client.writeRegisters(req, std::move(cb));
}

// isConnected() 在头文件里内联实现（直接读 client 的状态，保持单一事实源）

void ModbusSession::reconnect()
{
    if (m_reconnectTimer && m_reconnectTimer->isActive())
        return;   // 已经在重连流程里，不要重复启动定时器

    // 注意：这里不再自己关闭连接。m_ctx 归 client 的 worker（IO 线程）所有，
    // session 碰不到它 —— 这正是"线程封闭"带来的职责变化。
    // client 探测到连接级错误时已自行降级状态，session 只负责安排重连节奏。
    m_currentIntervalMs = 1000;
    m_reconnectTimer->start(m_currentIntervalMs);
    Logger::instance().warn(QStringLiteral("Reconnect scheduled, first attempt in %1ms")
                                .arg(m_currentIntervalMs));
}

void ModbusSession::attemptReconnect()
{
    Logger::instance().info(QStringLiteral("Attempting reconnect to %1:%2")
                                .arg(m_host).arg(m_port));
    m_reconnectTimer->stop();

    // 异步连接：不再阻塞工作线程（原来的 m_client.open() 会卡最长 2s）
    m_client.connectAsync([this](bool ok) {
        if (ok) {
            m_currentIntervalMs = 1000;
            Logger::instance().info("Reconnect succeeded");
        } else {
            m_currentIntervalMs = qMin(m_currentIntervalMs * 2, m_maxIntervalMs);
            m_reconnectTimer->start(m_currentIntervalMs);
            Logger::instance().warn(QStringLiteral("Reconnect failed, next attempt in %1ms")
                                        .arg(m_currentIntervalMs));
        }
        // connectionChanged 由 client 发出、本类透传，这里不需要再 emit
    });
}

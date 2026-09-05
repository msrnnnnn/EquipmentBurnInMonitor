#ifndef MODBUSSESSION_H
#define MODBUSSESSION_H

#include <QObject>
#include <QTimer>
#include <functional>
#include "modbustcpclient.h"

// ModbusSession —— 连接生命周期的"对外门面"
//
// 职责边界（异步化后更清晰）：
//   管：指数退避重连的节奏、对上层暴露统一接口、透传连接状态信号
//   不管：任何阻塞操作（连接 / 读写全在 client 的 IO 线程）
//
// 为什么状态不再自己存一份：client 手握连接事实（它是 connectionChanged 的源头），
// session 再存一份 m_connected 就会出现两份状态不一致的经典 bug。
// 所以 isConnected() 直接读 client —— 单一事实源。
class ModbusSession : public QObject
{
    Q_OBJECT
public:
    explicit ModbusSession(QObject *parent = nullptr);

    bool start(const QString &host, int port, int timeoutMs = 2000);   // 内部异步连接，调用后立即返回
    void stop();

    // 异步读写：cb 在本对象线程被调用（由 client 保证）
    void send(const ModbusReadRequest &req, std::function<void(ModbusResponse)> cb);
    void write(const ModbusWriteRequest &req, std::function<void(ModbusResponse)> cb);

    bool isConnected() const { return m_client.isConnected(); }

    // L4 子集：透传 client 的 stale 丢弃计数（门面 60s 摘要日志消费）
    int droppedStale() const { return m_client.droppedStale(); }

signals:
    // 透传 client 的信号：上层（main.cpp / ServiceFacade）的接口一行不用改
    void connectionChanged(bool connected);

public slots:
    void reconnect();           // 由 HealthMonitor::degraded 触发
private slots:
    void attemptReconnect();    // 退避定时器的单次尝试
private:
    // 值成员 + parent 设 this：session 被 moveToThread 时，client 作为子对象自动跟随，
    // 保证 client 始终和 session 在同一线程（回调才能正确回到工作线程）。
    ModbusTcpClient m_client;

    QTimer *m_reconnectTimer = nullptr;
    QString m_host;
    int m_port{502};
    int m_currentIntervalMs{1000};    // 退避起点
    int m_maxIntervalMs{60000};       // 退避上限
};

#endif // MODBUSSESSION_H

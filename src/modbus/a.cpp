#pragma once
#include "ModbusTypes.h"
#include <QObject>
#include <QMutex>
#include <QThread>
#include <deque>
#include <functional>
#include <atomic>

extern "C" {
#include "modbus.h"
}

// 只活在 IO 线程的 worker：收到请求就阻塞执行，完成回调给 client
class ModbusIoWorker : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    // 由 client 通过 invokeMethod(worker, lambda, QueuedConnection) 投递执行，
    // lambda 内调 client 的 performRead/performWrite（见下），执行完再 invokeMethod 回 client
};

class ModbusTcpClient : public QObject
{
    Q_OBJECT
public:
    explicit ModbusTcpClient(QObject *parent = nullptr);
    ~ModbusTcpClient() override;

    void configure(const QString &host, int port, int timeoutMs);
    bool start();            // 建 IO 线程 + moveToThread worker + 异步连接
    void stop();             // 停线程 + 断开 + 清队列

    bool isConnected() const { return m_connected.load(); }

    // ── 异步 API：入队后立即返回；cb 在【对象线程】被调用 ──
    void readRegisters(const ModbusReadRequest &req,
                       std::function<void(ModbusResponse)> cb);
    void writeRegisters(const ModbusWriteRequest &req,
                        std::function<void(ModbusResponse)> cb);

signals:
    void connectionChanged(bool connected);

private:
    struct PendingRequest {
        bool isWrite{false};
        ModbusReadRequest readReq;
        ModbusWriteRequest writeReq;
        std::function<void(ModbusResponse)> cb;
    };

    void enqueue(PendingRequest &&pr);   // 入队 + 尝试派发（对象线程）
    void dispatchNext();                 // 队首投给 IO 线程（对象线程）
    void onIoFinished(ModbusResponse rsp); // IO 完成回调（对象线程）

    // ── 以下只会在 IO 线程执行（阻塞） ──
    ModbusResponse performRead(const ModbusReadRequest &req);    // 锁内 modbus_read_*
    ModbusResponse performWrite(const ModbusWriteRequest &req);  // 锁内 modbus_write_*
    bool doConnect();    // modbus_new_tcp + connect（阻塞，丢 IO 线程）
    void doDisconnect(); // 锁内 close + free

    QThread *m_ioThread = nullptr;
    ModbusIoWorker *m_worker = nullptr;

    // 队列只被【对象线程】访问（入队在对象线程、取队首在对象线程）→ 无需锁
    std::deque<PendingRequest> m_queue;
    bool m_processing{false};

    QMutex m_ctxMutex;              // 唯一需要锁的：IO 线程执行 vs stop 释放
    modbus_t *m_ctx{nullptr};

    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_stopped{false};
    QString m_host;
    int m_port{502};
    int m_timeoutMs{2000};
};

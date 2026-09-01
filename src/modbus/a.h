#ifndef MODBUSTCPCLIENT_H
#define MODBUSTCPCLIENT_H

#include <QObject>       // 【Qt官方】信号槽 / invokeMethod / 线程归属能力
#include <QMutex>        // 【Qt官方】互斥锁
#include <QThread>       // 【Qt官方】IO 线程载体
#include <QString>
#include <deque>         // 【标准库】双端队列：装待执行请求
#include <functional>    // 【标准库】std::function：装"回调函数"的盒子
#include <atomic>        // 【标准库】原子变量：跨线程免锁读写
#include "ModbusTypes.h"

extern "C" {
#include "modbus.h"      // 【第三方库 libmodbus】C 接口
}

// ────────────────────────────────────────────────────────────────
// 【我们自己写的】ModbusIoWorker —— IO 线程的"收件箱"（空壳类，无逻辑）
//
// 为什么需要它：QMetaObject::invokeMethod 第一个参数必须是 QObject，事件
// 才能投进"那个对象所属线程"的事件队列。IO 线程里必须有属于它的 QObject
// 当投递目标，worker 就是这个锚点。它自己不干活 —— 真正干活的是下面
// client 的 performRead/performWrite（由 lambda 捕获后投递过去执行）。
//
// 为什么不让 client 自己 moveToThread 到 IO 线程：client 必须留在工作线程
// （readRegisters 被调度线程同线程调用、回调要在工作线程跑、队列是工作
// 线程私有物）。一个 QObject 只能属于一个线程，所以 client 是"总部"，
// worker 是"驻 IO 线程的办事处"，总部往办事处发文件。
// ────────────────────────────────────────────────────────────────
class ModbusIoWorker : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;   // 直接沿用父类构造，不需要任何成员
};

class ModbusTcpClient : public QObject   // 必须继承 QObject：要跨线程投递（去程/回程）
{
    Q_OBJECT
public:
    explicit ModbusTcpClient(QObject *parent = nullptr);
    ~ModbusTcpClient() override;   // 析构兜底调 stop()，防 IO 线程泄漏

    // ── 生命周期三段 ──
    // configure：只存连接参数，不做任何 IO（不是阻塞，任意线程可调）
    // start    ：建 IO 线程 → worker.moveTothread → thread.start()（事件循环跑起来）
    //            → 异步发起连接（modbus_connect 最长阻塞 2s，绝不能卡调用方）
    // stop     ：置 m_stopped → 清队列 → 等 IO 线程收尾 → 锁内关连接 → 退出线程
    void configure(const QString &host, int port, int timeoutMs = 2000);
    bool start();
    void stop();

    // 异步重连：把阻塞的 modbus_connect 也丢给 IO 线程，cb 在对象线程回调。
    // 供 ModbusSession 的指数退避重连用 —— 重连不再卡调度线程。
    void connectAsync(std::function<void(bool ok)> cb = nullptr);

    bool isConnected() const { return m_connected.load(); }  // 原子读：状态灯免锁查询

    // ── 异步读写：入队后立即返回，cb 在【对象线程】被调用 ──
    // 为什么不是"返回结果"：返回值 = 调用方必须等；回调 = 调用方先走，
    // 结果到了被叫回来。这是异步 API 的标志形态（和 asio 的 read(buf, cb) 一样）。
    void readRegisters(const ModbusReadRequest &req,
                       std::function<void(ModbusResponse)> cb);
    void writeRegisters(const ModbusWriteRequest &req,
                        std::function<void(ModbusResponse)> cb);

signals:
    // 连接状态变化。规则：信号从"最了解事实的地方"发 —— client 握着 m_ctx，
    // doConnect/doDisconnect 只有它第一时间知道，所以由它发。
    // ModbusSession 保留同名信号【透传】，上层（main.cpp）一行不用改。
    void connectionChanged(bool connected);

private:
    // ── 队列里的一格：请求数据 + 它的回调，绑死成一对 ──
    // 为什么绑一起：单飞队列是 FIFO，每个请求完成时必须找到"恰好属于它的
    // 回调"。分开存两个队列会错位（一个提前失败出队、一个正常完成）。
    struct PendingRequest {
        bool isWrite{false};                        // ① 读还是写
        ModbusReadRequest  readReq;                 // ② 读请求（unitId/地址/数量）
        ModbusWriteRequest writeReq;                //    写请求（unitId/地址/值）
        std::function<void(ModbusResponse)> cb;     // ③ 回调（调用方传的 lambda）
    };

    // ── 单飞循环三件套（全部只在【对象线程】执行 → 队列无需加锁）──
    void enqueue(PendingRequest &&pr);        // 入队尾 → 调 dispatchNext 试着派发
    void dispatchNext();                      // 队列非空且无在飞 → 队首整包搬到
    // m_inflight → invokeMethod 投去 IO 线程
    void onIoFinished(ModbusResponse rsp);    // IO 完成 → 取回 m_inflight.cb 调用
    // → 释放 m_processing → 再 dispatchNext

    // ── 只在【IO 线程】执行的阻塞操作（绝不碰队列和 m_processing）──
    ModbusResponse performRead(const ModbusReadRequest &req);    // 锁内 modbus_read_registers
    ModbusResponse performWrite(const ModbusWriteRequest &req);  // 锁内 modbus_write_*
    bool doConnect();      // modbus_new_tcp + modbus_connect（阻塞，故放 IO 线程）
    void doDisconnect();   // 锁内 modbus_close + modbus_free

    // ── 成员：每一个都对应一个具体问题 ──
    QThread *m_ioThread = nullptr;       // 专职等待的线程：阻塞挪到这里，调度线程才自由
    ModbusIoWorker *m_worker = nullptr;  // 投递目标（空壳，线程的锚点）

    // 队列 + 在飞位：类型相同（PendingRequest），因为"请求和回调必须一起搬"
    //   m_queue    = 候诊室：动态的，执行期间新请求照样入队，front 会变
    //   m_inflight = 诊室  ：固定槽位，永远装"正在 IO 线程执行的那一个"
    // → onIoFinished 只认 m_inflight.cb，不会因为新请求入队而拿错回调
    std::deque<PendingRequest> m_queue;
    PendingRequest m_inflight;
    bool m_processing{false};            // 单飞开关：true = 有一个请求在 IO 线程执行中

    // 唯一需要锁的成员：IO 线程执行时读 m_ctx，对象线程 stop 时释放 m_ctx。
    // 队列管的是"请求 vs 请求"，锁管的是"执行 vs 销毁"——两种竞争，都要防。
    QMutex m_ctxMutex;
    modbus_t *m_ctx = nullptr;

    std::atomic<bool> m_connected{false};   // 跨线程读（状态灯），原子免锁
    std::atomic<bool> m_stopped{false};     // stop 后回调一律丢弃，防对象销毁后还执行

    QString m_host;
    int m_port{502};
    int m_timeoutMs{2000};
};

#endif // MODBUSTCPCLIENT_H

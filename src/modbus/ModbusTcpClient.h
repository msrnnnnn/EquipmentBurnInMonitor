#ifndef MODBUSTCPCLIENT_H
#define MODBUSTCPCLIENT_H

#include <QObject>
#include <QThread>
#include <QString>
#include <deque>
#include <functional>
#include <atomic>
#include "ModbusTypes.h"

extern "C" {
#include "modbus.h"      // libmodbus C 接口
}

// ════════════════════════════════════════════════════════════════════════
// 连接状态机 —— 管"什么状态下允许什么操作"（逻辑正确性）
//
// 为什么需要它（锁替代不了的三件事）：
//   1. 防重复连接：HealthMonitor 触发重连 + 手动连接同时发生，没有 Connecting
//      态会连出第二个 modbus_t，泄漏一个
//   2. 快速失败：未连接时请求直接失败返回，不用傻等 2 秒超时
//   3. Stopped 终态：停止后一切请求与回调直接丢弃
//
// 注意它管不了什么：保护一段需要长时间持有的资源（那是锁或线程封闭的领域）。
// 本设计里 m_ctx 的独占靠"线程封闭"（归 worker），不靠状态机。
//
// 谁写状态：占坑（→Connecting）由对象线程在投递时写；结果（→Connected /
// Disconnected）由 IO 线程写。用 std::atomic 保证跨线程可见。
// ════════════════════════════════════════════════════════════════════════
enum class LinkState {
    Disconnected,   // 未连接：请求直接失败（快速失败，不等超时）
    Connecting,     // 连接中：拒绝重复连接（占坑态）
    Connected,      // 已连接：唯一允许派发请求的状态
    Stopped         // 终态：丢弃一切请求与回调
};

// ════════════════════════════════════════════════════════════════════════
// ModbusIoWorker —— IO 线程的"执行者"，唯一持有 m_ctx 的对象
//
// 为什么由它持有 m_ctx 而不是 client：
//   让"封装边界"与"线程边界"重合。worker 只活在 IO 线程，m_ctx 作为它的成员
//   天然只在 IO 线程被访问 —— "谁能访问"和"在哪个线程访问"是同一个答案。
//   于是：不需要任何锁，也不存在"我的私有成员我却不能碰"的别扭。
//
// 职责边界：只做执行，不做调度。队列、单飞、回调分派全在 client（对象线程）。
// ════════════════════════════════════════════════════════════════════════
class ModbusIoWorker : public QObject
{
    Q_OBJECT
public:
    explicit ModbusIoWorker(QObject *parent = nullptr);
    ~ModbusIoWorker() override;

    // 以下四个方法只在 IO 线程调用——只有它们访问 m_ctx，其余方法一概不碰。
    // 这个约束由结构保证（本对象只活在 IO 线程），Q_ASSERT 仅作 Debug 兜底。
    bool doConnect(const QString &host, int port, int timeoutMs);
    void doDisconnect();
    ModbusResponse performRead(const ModbusReadRequest &req);
    ModbusResponse performWrite(const ModbusWriteRequest &req);

private:
    modbus_t *m_ctx = nullptr;   // 唯一所有者是本对象 → 线程封闭 → 零锁
};

// ════════════════════════════════════════════════════════════════════════
// ModbusTcpClient —— 异步调度层（活在对象线程 / 工作线程）
//
// 职责边界：
//   管：请求队列、单飞派发、回调分派、连接状态机、生命周期
//   不管：任何 libmodbus 调用（全部在 worker）
//
// 为什么整个类零互斥锁：
//   - 队列 / m_inflight / m_processing 只在对象线程访问 → 天然无锁
//   - m_ctx 在 worker 里、只在 IO 线程碰 → 天然无锁
//   正确性由"线程归属"这个结构保证，而不是"记得加锁"的纪律。
// ════════════════════════════════════════════════════════════════════════
class ModbusTcpClient : public QObject
{
    Q_OBJECT
public:
    // L2（背压审查）：staleThresholdMs —— 读请求在队列里等待超过该阈值视为过期，
    // 派发前直接作废（失败回调返回，不算链路错误）。构造参数带默认值，调用方无需改动。
    explicit ModbusTcpClient(QObject *parent = nullptr, qint64 staleThresholdMs = 5000);
    ~ModbusTcpClient() override;

    // L4 子集：被 stale 丢弃的读请求计数（门面 60s 摘要日志消费）
    int droppedStale() const { return m_droppedStale.load(); }

    // ── 生命周期三段 ──
    void configure(const QString &host, int port, int timeoutMs = 2000);  // 只存参数，不做 IO
    bool start();   // 建 IO 线程 + 异步连接（立即返回，不阻塞调用方）
    void stop();    // 清队列 + 阻塞投递 doDisconnect + 线程退出（关闭时唯一一次等待）

    // 异步重连：供 ModbusSession 的指数退避重连用，连接不再卡调度线程
    void connectAsync(std::function<void(bool ok)> cb = nullptr);

    bool isConnected() const { return m_state.load() == LinkState::Connected; }
    LinkState state() const { return m_state.load(); }

    // ── 异步读写：入队后立即返回；cb 在对象线程被调用 ──
    // 为什么不是"返回结果"：返回值 = 调用方必须等；回调 = 调用方先走，结果到了被叫回来。
    void readRegisters(const ModbusReadRequest &req,
                       std::function<void(ModbusResponse)> cb);
    void writeRegisters(const ModbusWriteRequest &req,
                        std::function<void(ModbusResponse)> cb);

signals:
    // 由 client 发（它是连接状态的事实源），ModbusSession 保留同名信号透传给上层
    void connectionChanged(bool connected);

private:
    // 队列里的一格：请求数据 + 它的回调，绑死成一对。
    // 为什么绑一起：单飞 FIFO 里，每个请求完成时必须找到"恰好属于它的回调"；
    // 分开存两个队列会错位（一个提前出队、一个正常完成 → 对不上号）。
    struct PendingRequest {
        bool isWrite{false};                        // ① 读还是写（tagged union 的"标签"）
        ModbusReadRequest  readReq;                 // ② 读请求（unitId / 地址 / 数量）
        ModbusWriteRequest writeReq;                //    写请求（unitId / 地址 / 值）
        std::function<void(ModbusResponse)> cb;     // ③ 回调（调用方传的 lambda）
        qint64 enqueuedAtMs{0};                     // ④ 入队时间戳（L2 stale 判定的依据）
    };

    // 单飞循环三件套 —— 全部只在对象线程执行，所以队列无需加锁
    void enqueue(PendingRequest &&pr);        // 入队尾 → 试着派发
    void dispatchNext();                      // 队列非空且无在飞 → 整包搬到 m_inflight → 投给 IO 线程
    void onIoFinished(ModbusResponse rsp);    // IO 完成 → 取回 cb 调用 → 再派发下一个
    void onConnectFinished(bool ok);          // 连接完成 → 发信号 → 派发队列里等着的请求

    QThread *m_ioThread = nullptr;       // 专职等待的线程：阻塞挪到这，调度线程才自由
    ModbusIoWorker *m_worker = nullptr;  // moveToThread 到 IO 线程，且唯一持有 m_ctx

    // 队列与在飞位：类型相同，因为"请求和回调必须一起搬移"才能保证一一对应
    //   m_queue    = 候诊室（动态：执行期间新请求照样入队，front 会变）
    //   m_inflight = 诊室  （固定槽位：永远装"正在 IO 线程执行的那一个"）
    //   → onIoFinished 只认 m_inflight.cb，不会因为新请求入队而拿错回调
    std::deque<PendingRequest> m_queue;
    PendingRequest m_inflight;
    bool m_processing{false};            // 单飞开关：true = 有一个请求正在 IO 线程执行

    std::function<void(bool)> m_connectCb;   // 当前连接任务的回调（状态机保证同时仅一个）

    std::atomic<LinkState> m_state{LinkState::Disconnected};  // 对象线程读，IO 线程写
    std::atomic<bool> m_stopped{false};                       // stop 后回调一律丢弃

    QString m_host;
    int m_port{502};
    int m_timeoutMs{2000};
    qint64 m_staleThresholdMs{5000};          // L2：读请求等待上限
    std::atomic<int> m_droppedStale{0};       // L4 子集：stale 丢弃计数（跨线程读）
};

#endif // MODBUSTCPCLIENT_H

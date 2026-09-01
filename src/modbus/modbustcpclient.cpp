#include "modbustcpclient.h"
#include "logging/logger.h"
#include <QVector>
#include <QMetaObject>
#include <QThread>
#include <winsock2.h>

using burninsys::Logger;

namespace {
// ── 判断 errno 是否为"连接级"错误 ──
// 只有这类错误才需要触发重连并清空队列；协议异常（非法功能码 / 非法地址 /
// 非法数据）说明请求本身有问题，连接还好好活着，重连只会把链路搞得更乱。
bool isLinkError(int err)
{
    return err == ETIMEDOUT || err == ECONNRESET || err == EPIPE ||
           err == ENOTCONN || err == EBADF;
}
}

// ═══════════════════════════════════════════════════════════════════════
// ModbusIoWorker —— 只在 IO 线程执行，唯一持有 m_ctx
// ═══════════════════════════════════════════════════════════════════════

ModbusIoWorker::ModbusIoWorker(QObject *parent) : QObject(parent) {}

ModbusIoWorker::~ModbusIoWorker()
{
    doDisconnect();   // 兜底：正常路径由 client::stop 调用，这里防遗漏
}

bool ModbusIoWorker::doConnect(const QString &host, int port, int timeoutMs)
{
    if (m_ctx)
        doDisconnect();   // 防重复连接泄漏旧 ctx（状态机外的最后一道保险）

    m_ctx = modbus_new_tcp(host.toUtf8().constData(), port);
    if (m_ctx == nullptr) {
        Logger::instance().error("Modbus TCP init failed: modbus_new_tcp returned nullptr.");
        return false;
    }

    // 【libmodbus API】响应超时：设备无响应时最多等这么久，避免 IO 线程被无限拖住
    modbus_set_response_timeout(m_ctx, static_cast<uint32_t>(timeoutMs / 1000),
                                static_cast<uint32_t>((timeoutMs % 1000) * 1000));

    if (modbus_connect(m_ctx) == -1) {
        const int savedErrno = errno;
        const int wsaErr = WSAGetLastError();
        modbus_close(m_ctx);
        modbus_free(m_ctx);
        m_ctx = nullptr;
        Logger::instance().error(QStringLiteral("Modbus TCP connect failed: errno=%1 wsaErr=%2")
                                     .arg(savedErrno).arg(wsaErr));
        return false;
    }

    Logger::instance().info("Modbus TCP connect success");
    return true;
}

void ModbusIoWorker::doDisconnect()
{
    if (m_ctx) {
        modbus_close(m_ctx);
        modbus_free(m_ctx);
        m_ctx = nullptr;
    }
}

ModbusResponse ModbusIoWorker::performRead(const ModbusReadRequest &req)
{
    if (m_ctx == nullptr)
        return ModbusResponse{false, {}, "Not connected", true};

    QVector<uint16_t> buffer(req.quantity);
    modbus_set_slave(m_ctx, req.unitId);
    const int rc = modbus_read_registers(m_ctx, req.startAddress, req.quantity, buffer.data());
    const int savedErr = errno;

    ModbusResponse rsp;
    if (rc != -1) {
        rsp.success = true;
        rsp.payload = QByteArray(reinterpret_cast<const char *>(buffer.data()),
                                 rc * static_cast<int>(sizeof(uint16_t)));
    } else {
        Logger::instance().info(QStringLiteral("modbus read registers failed: %1")
                                    .arg(modbus_strerror(savedErr)));
        rsp.success = false;
        rsp.error = modbus_strerror(savedErr);
        rsp.linkBroken = isLinkError(savedErr);
    }
    return rsp;
}

ModbusResponse ModbusIoWorker::performWrite(const ModbusWriteRequest &req)
{
    if (m_ctx == nullptr)
        return ModbusResponse{false, {}, "Not connected", true};
    if (req.values.isEmpty())
        return ModbusResponse{false, {}, "Empty values", false};

    modbus_set_slave(m_ctx, req.unitId);
    int rc = -1;
    if (req.type == RegisterType::HoldingRegister)
        rc = modbus_write_registers(m_ctx, req.startAddress, req.values.size(), req.values.data());
    else if (req.type == RegisterType::Coil)
        rc = modbus_write_bit(m_ctx, req.startAddress, req.values[0]);
    const int savedErr = errno;

    ModbusResponse rsp;
    if (rc != -1) {
        rsp.success = true;
    } else {
        Logger::instance().info(QStringLiteral("modbus write failed: %1")
                                    .arg(modbus_strerror(savedErr)));
        rsp.success = false;
        rsp.error = modbus_strerror(savedErr);
        rsp.linkBroken = isLinkError(savedErr);
    }
    return rsp;
}

// ═══════════════════════════════════════════════════════════════════════
// ModbusTcpClient —— 异步调度层，活在对象线程
// ═══════════════════════════════════════════════════════════════════════

ModbusTcpClient::ModbusTcpClient(QObject *parent) : QObject(parent) {}

ModbusTcpClient::~ModbusTcpClient()
{
    stop();
}

void ModbusTcpClient::configure(const QString &host, int port, int timeoutMs)
{
    m_host = host;
    m_port = port;
    m_timeoutMs = timeoutMs;
}

bool ModbusTcpClient::start()
{
    if (m_ioThread)
        return false;   // 幂等：已经启动过就直接返回

    m_stopped = false;
    m_state.store(LinkState::Disconnected);

    // 【Qt API】建 IO 线程并把 worker 搬过去。
    // 注意：worker 不能有 parent（有 parent 的对象不允许 moveToThread），
    // 所以两个对象都在 stop() 里手动释放。
    m_ioThread = new QThread;
    m_worker = new ModbusIoWorker;
    m_worker->moveToThread(m_ioThread);
    m_ioThread->start();

    connectAsync();   // 连接本身也是阻塞的（最长约 2s），异步发起，不卡调用方
    return true;
}

void ModbusTcpClient::stop()
{
    if (!m_ioThread)
        return;

    m_stopped = true;
    m_queue.clear();
    m_inflight = PendingRequest{};
    m_processing = false;
    m_connectCb = nullptr;

    // 【Qt API】阻塞投递：等 IO 线程把手上的活干完、并把 doDisconnect 执行完。
    // 用 BlockingQueuedConnection 是为了"保证 m_ctx 一定被释放"—— quit() 不保证
    // 队列里剩余事件被执行，而阻塞投递会一直等到它执行完。
    // 代价：若 IO 正卡在请求超时，这里最多等一个超时周期 —— 只在关闭时发生一次。
    QMetaObject::invokeMethod(m_worker, [this]() {
        m_worker->doDisconnect();
    }, Qt::BlockingQueuedConnection);

    m_ioThread->quit();
    m_ioThread->wait();   // 等线程真正停下，之后的 delete 没有任何并发风险

    delete m_worker;
    m_worker = nullptr;
    delete m_ioThread;
    m_ioThread = nullptr;

    m_state.store(LinkState::Stopped);
}

void ModbusTcpClient::connectAsync(std::function<void(bool ok)> cb)
{
    const LinkState s = m_state.load();

    // 状态机门禁：连接中 / 已连接 / 已停止 都不再发起新连接。
    // 没有这道门，HealthMonitor 的重连和用户手动连接会撞在一起，
    // 连出第二个 modbus_t —— 旧的那个再也没人释放。
    if (s == LinkState::Connecting || s == LinkState::Connected || s == LinkState::Stopped) {
        if (cb)
            cb(s == LinkState::Connected);
        return;
    }

    m_connectCb = std::move(cb);
    m_state.store(LinkState::Connecting);   // 占坑：立刻改状态，防止同一时刻再次入队

    const QString host = m_host;
    const int port = m_port;
    const int timeout = m_timeoutMs;

    // 去程投递：阻塞的 modbus_connect 放到 IO 线程执行
    QMetaObject::invokeMethod(m_worker, [this, host, port, timeout]() {
        const bool ok = m_worker->doConnect(host, port, timeout);
        m_state.store(ok ? LinkState::Connected : LinkState::Disconnected);

        // 回程投递：结果回对象线程，由 onConnectFinished 统一处理
        QMetaObject::invokeMethod(this, [this, ok]() {
            onConnectFinished(ok);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void ModbusTcpClient::onConnectFinished(bool ok)
{
    if (m_stopped)
        return;

    auto cb = std::move(m_connectCb);   // 先取走回调，再干别的
    if (cb)
        cb(ok);
    emit connectionChanged(ok);

    if (ok)
        dispatchNext();   // 连接刚建立，把排队期间攒下的请求派出去
}

void ModbusTcpClient::readRegisters(const ModbusReadRequest &req,
                                    std::function<void(ModbusResponse)> cb)
{
    // 状态机门禁：未连接直接失败返回（快速失败），不用等一个超时周期才知道连不上
    if (m_state.load() != LinkState::Connected) {
        if (cb)
            cb(ModbusResponse{false, {}, "Not connected", true});
        return;
    }
    enqueue(PendingRequest{false, req, ModbusWriteRequest{}, std::move(cb)});
}

void ModbusTcpClient::writeRegisters(const ModbusWriteRequest &req,
                                     std::function<void(ModbusResponse)> cb)
{
    if (m_state.load() != LinkState::Connected) {
        if (cb)
            cb(ModbusResponse{false, {}, "Not connected", true});
        return;
    }
    enqueue(PendingRequest{true, ModbusReadRequest{}, req, std::move(cb)});
}

void ModbusTcpClient::enqueue(PendingRequest &&pr)
{
    if (m_stopped)
        return;
    m_queue.push_back(std::move(pr));
    dispatchNext();
}

void ModbusTcpClient::dispatchNext()
{
    if (m_processing || m_queue.empty())
        return;
    if (m_state.load() != LinkState::Connected)
        return;   // 断连期间不派发，请求留在队列里等重连成功

    m_processing = true;
    m_inflight = std::move(m_queue.front());   // 整包搬移：请求 + 它的回调一起走
    m_queue.pop_front();

    const bool isWrite = m_inflight.isWrite;
    const ModbusReadRequest readReq = m_inflight.readReq;
    const ModbusWriteRequest writeReq = m_inflight.writeReq;

    // 去程投递：阻塞的 libmodbus 调用只在 IO 线程执行。
    // 请求数据按值捕获 —— 跨线程是拷贝，不共享内存，所以不需要任何锁。
    QMetaObject::invokeMethod(m_worker, [this, isWrite, readReq, writeReq]() {
        ModbusResponse rsp = isWrite ? m_worker->performWrite(writeReq)
                                     : m_worker->performRead(readReq);

        // 回程投递：把结果送回对象线程，回调在那边被调用
        QMetaObject::invokeMethod(this, [this, rsp]() {
            onIoFinished(rsp);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void ModbusTcpClient::onIoFinished(ModbusResponse rsp)
{
    // 顺序关键：必须先把回调从"在飞位"取走。
    // 后面 dispatchNext() 会把新请求搬进 m_inflight，覆盖掉还没调用的旧回调。
    auto cb = std::move(m_inflight.cb);
    m_processing = false;

    // 连接级错误才降级：状态转回 Disconnected 并通知上层（Session 的退避重连由此触发），
    // 同时清空队列 —— 断线期间攒下的请求已经过期，恢复后重发一遍旧的没有意义。
    if (!rsp.success && rsp.linkBroken) {
        if (m_state.load() == LinkState::Connected) {
            m_state.store(LinkState::Disconnected);
            emit connectionChanged(false);
        }
        m_queue.clear();
    }

    if (!m_stopped && cb)
        cb(rsp);

    dispatchNext();   // 派下一个，流水线继续转
}

# EquipmentBurnInMonitor 设备老化测试监控系统

面向工业设备老化测试（Burn-In Test）场景的 Qt 上位机监控软件。通过 Modbus TCP 实时采集电机运行遥测（温度 / 电流 / 转速 / 振动 / 电压 / 功率 共 6 路），完成规则判定、SQLite 历史存储、工业大屏展示与设备启停控制，并可自动执行 72 小时老化测试并给出 PASS/FAIL 判定。

> 本项目由作者独立开发，同时作为 Qt 客户端开发方向的求职作品。

---

## 功能特性

- **Modbus TCP 异步采集**：基于 libmodbus（C 源码随项目编译）轮询 7 路寄存器（6 遥测 + 运行状态），支持 32 位 IEEE754 浮点（温度）与 16 位整数 × 缩放系数两种解码；请求走 FIFO 队列 + 单飞派发，阻塞调用隔离在专用 IO 线程
- **内置 Modbus 从站模拟器**：`-s` 一键启动内置 QTcpServer 从站（MBAP 报文解析、03/04/06/10 功能码、寄存器随机游走、启停线圈镜像到运行状态寄存器），无硬件即可跑通全链路
- **规则引擎**：阈值上下限（`ThresholdRule`）+ 变化率突变（`RateChangeRule`）两类规则，JSON 配置驱动，触发即历史表格异常行标红 + 日志留痕
- **控制闭环**：UI → Provider → ServiceFacade → ModbusSession 三条下行链路——启停（Coil 0）、阈值批量下发、模式切换；写入均带回调，失败留日志而非静默；启停命令由从站镜像到运行状态寄存器，UI 显示"运行: 是/否"作为设备侧已执行的证据
- **工业大屏 UI**：6 指标卡片、QCustomPlot 30 秒滑动窗口实时曲线（异步合并重绘）、7 列历史表格（异常行高亮）、连接状态灯、设备运行状态、规则触发计数、测试倒计时与 PASS/FAIL
- **SQLite 持久化**：WAL 模式窄表（`id/ts_ms/name/value/quality`）毫秒级时间戳，独立存储线程异步落库；写失败 3 次重试后落 `.cache` 降级文件，数据不丢；坏数据带质量标记保留不静默丢弃
- **可靠性**：健康监测（10s 采样龄判定降级/恢复）+ 通信层即时感知断连 + 指数退避自动重连（1s → 60s 封顶）+ 异常触发 10Hz 高频采样（温度 >80°C 或电流 >25A 时）
- **老化测试判定**：TestRunner 引擎按测试规程倒计时，记录温度/振动峰值，到期自动判定合格与否；规程可在设置页实时修改（改时长 → 倒计时立即生效）
- **配置热更新**：JSON 配置 + QFileSystemWatcher 监听，修改配置实时重建规则与采集任务

## 技术栈

| 类别 | 选型 |
|---|---|
| UI | Qt 6.10 (Widgets) + QCustomPlot |
| 语言/构建 | C++17 / CMake (AUTOMOC) |
| 通信 | libmodbus 3.2.0（`third_party/libmodbus`，`extern "C"` 混编，链接 `ws2_32`） + Qt Network（内置模拟器） |
| 存储 | SQLite（Qt Sql 模块，WAL 模式） |
| 并发 | QThread + moveToThread + 信号槽队列连接 + 线程封闭（零互斥锁） + QReadWriteLock |

## 架构

### 分层

```
展示层      MainWindow / HomePage / SettingsPage / VideoPage（占位）
服务层      ServiceFacade（门面：统一编排与生命周期）
调度与规则   PollingScheduler / RuleEngine / ThresholdRule / RateChangeRule
设备抽象层   DeviceDriver（模板方法）→ ModbusDeviceDriver → 3 个具体驱动
通信层      ModbusSession / ModbusTcpClient / ModbusIoWorker / libmodbus
数据层      DataCache（最新值快照）/ SqliteRepository（历史）
诊断层      HealthMonitor / DiagnosticsReporter / MetricsCollector
配置层      ConfigLoader / ConfigWatcher
模拟层      ModbusSimulator（内置从站，仅 -s 启用）
基础设施     Logger（单例 + Sink）
```

### 核心模块

| 模块 | 职责 | 关键点 |
|---|---|---|
| `ServiceFacade` | 系统编排中枢 | 门面模式；`start()/stop()/configure()`；跨线程调度 |
| `EquipmentData` / `EquipmentDataProvider` | 遥测数据模型 | 6 个 `Q_PROPERTY` + NOTIFY 信号；Provider 做历史缓存（50 条）与信号桥 |
| `PollingScheduler` | 轮询调度 | 100ms tick 主循环，每任务按 `intervalMs` 到期执行；单指标 `pending` 防重发 |
| `ModbusSession` | 会话与重连 | 异步读写 API；指数退避重连（1s→60s）；连接状态透传 client（单一事实源） |
| `ModbusTcpClient` | 异步调度层 | FIFO 队列 + 单飞派发 + `LinkState` 状态机；回调回投对象线程 |
| `ModbusIoWorker` | 阻塞调用执行者 | **唯一持有 `m_ctx`**，只活在 IO 线程 → 线程封闭，全类零锁 |
| `ModbusSimulator` | 内置 Modbus 从站 | `QTcpServer` 多客户端；MBAP 大端解析；03/04/06/10 功能码 + 异常响应 |
| `SqliteRepository` | 持久化 | 独立连接名；WAL + busy_timeout + synchronous=NORMAL；`save` 异步入队 |
| `RuleEngine` | 规则执行 | 策略模式；`std::unique_ptr<Rule>` 持有；触发发 `ruleTriggered` |
| `HealthMonitor` | 健康状态 | 2s 周期检查：连接 + 10s 内有 good 采样；`degraded/recovered` 边沿触发 |
| `TestRunner` | 老化测试引擎 | 倒计时（`tick` 每秒）、峰值记录、`evaluateVerdict()` 判定 |
| `Logger` | 日志 | 梅耶斯单例；`Sink = std::function` 可插拔；控制台 + 文件 |

### 设计模式

- **门面**：ServiceFacade 聚合 10+ 模块，UI 只面对它
- **模板方法**：DeviceDriver 定义 `name/buildReadRequest/decode` 三接口，具体驱动只实现"读哪个寄存器、怎么解码"
- **策略**：Rule 抽象 + ThresholdRule/RateChangeRule；Logger 的 Sink 也是
- **观察者**：信号槽全套；Q_PROPERTY NOTIFY 属性系统
- **单例**：Logger（梅耶斯单例，C++11 线程安全）
- **简单工厂**：PollingScheduler 按配置项名称创建具体设备驱动
- **线程封闭**：`m_ctx` 归 IO 线程内的 worker 独占，用"对象归属"替代互斥锁

## 数据流

### 采集上行

```
PollingScheduler::tick（100ms，工作线程）
  → 到点且该指标无在途请求 → 置 pending，记 lastPollMs（发起即记账）
  → ModbusSession::send → ModbusTcpClient::readRegisters（入队，立即返回）
  → 单飞派发：整包（请求 + 回调）搬到 m_inflight → invokeMethod 投给 IO 线程
  → ModbusIoWorker::performRead（libmodbus 阻塞读写，只在此处发生）
  → 回投对象线程 onIoFinished：取回回调执行 → 清 pending → 派发下一个
  → driver.decode(rsp) → TelemetrySample（name/value/quality/timestampMs）→ emit sampleReady
      ├─ HealthMonitor::onSample（刷新采样龄）
      └─ ServiceFacade::onSampleReady（主线程）
           ├─ setXxx() → EquipmentData 信号 → UI 卡片 / 曲线
           ├─ 攒 pendingRecord（time+6指标+anomaly=8 字段）→ 表格（anomaly 标红）
           ├─ good 数据 → RuleEngine.evaluate → ruleTriggered → 标红 + 日志
           ├─ TestRunner.recordSample（峰值） / DataCache.put / MetricsCollector.record
           └─ invokeMethod → SqliteRepository.save（工作线程队列）
```

### 控制下行

```
UI 按钮（启停/保存阈值/模式）
  → EquipmentDataProvider 信号（motorCommand/thresholdUpdated/modeChanged）
  → ServiceFacade 槽 → 构造 ModbusWriteRequest
  → invokeMethod(QueuedConnection) → ModbusSession::write(req, 回调)
  → 入 client 队列 → IO 线程执行 → 回调回到工作线程
  → 写失败在回调内 Logger::warn 留痕
```

### 规则判定

仅 `quality == "good"` 的样本进入规则评估；`bad` 样本仍落库/上表（可审计），但不进入统计与规则。

## 线程模型

```
主线程（UI）           MainWindow/Pages、ServiceFacade、RuleEngine、TestRunner、
                       HealthMonitor、MetricsCollector、DataCache
工作线程 m_workThread  ModbusSession、ModbusTcpClient、PollingScheduler（moveToThread）
存储线程 m_dbThread    SqliteRepository（S8：磁盘 IO 与采集解耦）
IO 线程（client 内建） ModbusIoWorker（唯一持有 m_ctx）+ libmodbus 阻塞调用
```

- **为什么单独开 IO 线程**：libmodbus 的 `modbus_connect` / `modbus_read_registers` 都是同步阻塞。放到专用线程后，设备变慢只会让某个指标的数据晚到，不会拖住整条调度循环——这是异步化的实际收益。
- **为什么存储再开一线程（S8）**：SQLite 写失败重试最多会睡 1.5s，放采集线程会直接拖住采样节奏；独立线程后磁盘慢只影响落库延迟，采集照常 1Hz/10Hz。
- **为什么零互斥锁**：`m_ctx` 归 worker 独占且只活在 IO 线程；队列 / `m_inflight` / `m_processing` 只在对象线程访问。正确性由"线程归属"这个结构保证，而不是"记得加锁"的纪律。
- 跨线程通信全部走信号槽 QueuedConnection / `QMetaObject::invokeMethod`，无裸共享内存。
- 停止顺序：先 `BlockingQueuedConnection` 停 Session/Scheduler，再 `quit()+wait()` 两个线程；`SqliteRepository` 的析构（`close + removeDatabase`）以 `BlockingQueuedConnection` 投递到存储线程执行——连接在哪创建，就在哪回收。
- 共享数据用锁：DataCache / MetricsCollector（QReadWriteLock）、Logger（QMutex）。

## 目录结构

```
EquipmentBurnInMonitor/
├── main.cpp                    # 入口：日志 → 配置 → 模拟器(-s) → ServiceFacade → MainWindow
├── mainwindow.{h,cpp}          # 导航壳（QListWidget + QStackedWidget + QSplitter）
├── equipmentdata.{h,cpp}       # EquipmentData（Q_PROPERTY 遥测模型）
├── equipmentdataprovider.{h,cpp} # Provider（历史缓存 + 信号桥）
├── config.json                 # 运行配置（热更新）
├── pages/                      # homepage（大屏）/ settingspage / videopage（占位）
├── src/
│   ├── api/ServiceFacade       # 门面
│   ├── config/                 # ConfigLoader + ConfigWatcher
│   ├── data/                   # DataCache + SqliteRepository
│   ├── device/                 # DeviceDriver / ModbusDeviceDriver / 3 驱动
│   ├── diagnostics/            # HealthMonitor / DiagnosticsReporter（未实例化）
│   ├── logging/                # Logger
│   ├── metrics/                # MetricsCollector
│   ├── modbus/                 # ModbusTypes / ModbusTcpClient / ModbusIoWorker / ModbusSession
│   ├── rules/                  # Rule / RuleEngine / ThresholdRule / RateChangeRule
│   ├── scheduler/              # PollingScheduler
│   ├── simulator/              # ModbusSimulator（内置从站，-s 启用）
│   └── test/                   # TestRunner
└── third_party/                # libmodbus 3.2.0（C 源码）/ QCustomPlot
```

## 构建与运行

前置：Qt 6.10.0 (MinGW)、CMake ≥ 3.16。

```bash
cmake -S . -B build
cmake --build build
# 运行（构建目录内，config.json 会被 COPYONLY 到构建目录）
./EquipmentBurnInMonitor          # 连接真实设备 / 外部模拟器
./EquipmentBurnInMonitor -s       # 启动内置 Modbus 从站模拟器，无需硬件
```

`-s` 会先在 `127.0.0.1:502` 拉起内置从站并给 6 个遥测寄存器写入合理初值，再按 1 秒步长做带限随机游走（温度 18~35°C、电流 8~16A、转速 1400~1600rpm 等），功率区间刻意跨过 8.0kW 阈值、电流步长刻意放大到 ±3A/s，以便演示阈值告警与变化率告警触发。

不使用 `-s` 时，程序启动后连接 `config.json` 中的 `endpoint`（默认 `127.0.0.1:502`），需要真实的 Modbus 设备或第三方模拟器。

## 配置说明（config.json）

| 字段 | 说明 |
|---|---|
| `endpoint` | Modbus TCP 端点（host / port / unitId / timeoutMs） |
| `testProfile` | 老化测试规程（时长 72h、合格阈值 85°C / 4.5mm/s） |
| `motorCommand` | 启停控制（Coil 地址 0，启动 0xFF00 / 停止 0x0000） |
| `modeCommand` | 模式切换（保持寄存器地址 200） |
| `items[]` | 采集项：name / address / scale（寄存器原始值 × scale = 实际值） |
| `rules[]` | 规则：type（threshold/rate）、threshold、rateLimit、windowSeconds、isUpper |
| `thresholdRegisters[]` | 阈值下发寄存器映射（地址 100~105） |
| `dataFilePath` | SQLite 数据库路径 |

修改 config.json 后由 ConfigWatcher 热更新（重建规则与采集任务，连接/存储不重启）。

## 数据存储

- 表 `telemetry(id INTEGER PRIMARY KEY AUTOINCREMENT, ts_ms, name, value, quality)`，三个索引：`idx_telemetry_ts` / `idx_telemetry_name` / `idx_name_ts(name, ts_ms)`
- WAL 模式：读写不互斥；`busy_timeout=5000`；`synchronous=NORMAL`
- 写失败降级：`save()` 单次插入失败后重试 2 次（间隔 500ms），仍失败则追加到 `<dbPath>.cache` 文件，宁可慢也不丢数据（S2）
- 热数据（内存 50 条宽表，UI 表格）与冷数据（SQLite 窄表，全量历史）分层

## 开发状态

### 已实现

- [x] Modbus TCP 采集全链路（7 路遥测、坏数据标记、多驱动解码）
- [x] **异步通信架构**（FIFO 队列 + 单飞派发 + IO 线程 + 状态机 + 回调）
- [x] **内置 Modbus 从站模拟器**（`-s` 一键演示，无硬件跑通全链路）
- [x] 规则引擎（阈值上下限 + 变化率，参数已调至可演示触发）与异常标红
- [x] 控制闭环（启停 / 阈值下发 / 模式切换写寄存器，带回调校验；**启停回读可见**）
- [x] **SQLite 加固**（id 主键 + 三索引 + 析构 removeDatabase）+ **降级写入**（重试 + .cache）
- [x] **三线程模型**（采集 / 存储 / IO 分离，S8）
- [x] **异常触发高频采样**（温度 >80°C 或电流 >25A → 10Hz，实测边沿切换）
- [x] **ServiceFacade 状态属性体系**（25 个 Q_PROPERTY + 无参 NOTIFY，S5）
- [x] **UI 属性绑定**（HomePage/SettingsPage，含规则状态卡 / 阈值快照 / 连接状态，S6）
- [x] 工业大屏 UI（卡片 / 曲线 / 表格 / 倒计时 / PASS-FAIL）
- [x] 健康监测 + 通信层断连感知 + 指数退避自动重连
- [x] 老化测试判定引擎（TestRunner：bad 不入峰值 / 人为中止判 aborted / 分钟级时长）
- [x] **热更新全量重启（S9）**：改配置 → `stop → start` 全量生效；ConfigWatcher 校验新配置，**无效配置拒绝切换、保持旧配置运行**
- [x] 帧聚合超时兜底（按 items 名单核对 + 1.5s 强制推送，丢采样不再串帧）
- [x] 历史表格增量刷新（每秒 7 个 item，不再全量重建 350 个）
- [x] 曲线双 Y 轴（温度左轴 + 电流右轴）
- [x] **四类功能码读链路**（01 线圈 / 02 离散输入 / 03 保持 / 04 输入，config `registerType` 驱动）
- [x] **规则自动停机联动**（autoStop 规则命中且电机运行中 → 自动写停机线圈；实测"启动→回读 1→超限→停机→回读 0"闭环）
- [x] 阈值快照"写成功才回写"（6 写收齐后回主线程更新，失败保留旧快照并提示）
- [x] 配置加载与热更新（规则 + 采集任务 + 连接 + TestRunner 全量重建）
- [x] Logger 单例 + Sink、MetricsCollector、DataCache

### 路线图（按演示与面试价值排序）

**P3 —— 工程化收尾**

1. 自动化测试（未做：纯逻辑与 UI 均无自动化用例，验证靠手动演示与实测实验）
2. 断线分层验收实验固化（拔网线时间线已实测：响应超时 2s → 断连感知 → 1s 重连，connect 失败才指数退避）

> 施工计划与文档出处见 `DEVELOPMENT_PLAN.md`（S1~S9 已全部完成）；源码级缺陷与面试追问防线见 `INTERVIEW_READINESS_REPORT.md`；本分支的设计决策、取舍与 Bug 排查全记录见 `BRANCH_NOTES.md`。

### 已知限制

- 01/02（线圈/离散输入）功能码链路已通，但驱动解码层只服务寄存器格式（bit 类采集项会标 bad，属诚实失败）
- `DiagnosticsReporter` 已实现但未实例化；`DataCache` 只写不读（快照 API 供诊断预留，Metrics 摘要已接 60s 日志消费者）
- 无自动化测试（验证靠手动演示 + 可复现的实测实验，见 `BRANCH_NOTES.md` 实测记录表）
- 视频页为占位骨架（视频模块不在路线图内）

## 第三方依赖

- **libmodbus 3.2.0**：`third_party/libmodbus/`，C 源码随项目编译（`modbus.c / modbus-data.c / modbus-tcp.c / modbus-rtu.c`），仅使用 TCP 客户端路径
- **QCustomPlot**：`third_party/qcustomplot.{h,cpp}`，实时曲线绘图

> Windows 上需定义 `FD_SETSIZE=8192`：libmodbus 会把 socket 句柄值 ≥ `FD_SETSIZE` 的连接当异常拒绝（errno=EINVAL），Qt 程序启动后句柄基数高，默认 1024 不够。

## 参考文档

- `BRANCH_NOTES.md`：`feat/simulator-async` 分支全记录——每个模块做了什么 / 为什么这么做 / 设计取舍 / 遇到的 Bug 与排查过程 / 实测数据
- `DEVELOPMENT_PLAN.md`：对照开发文档的差距分析与施工计划（S1~S9，已全部完成）
- `INTERVIEW_READINESS_REPORT.md`：源码级评审，按"面试官多快能发现"分级
- 开发基线文档：《设备老化测试监控系统-50次迭代开发文档》（43 节，位于项目外）

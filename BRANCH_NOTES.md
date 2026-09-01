# BRANCH_NOTES — `feat/simulator-async` 分支全记录

> 本文件记录该分支的**全部实质改动**：每个模块做了什么、为什么这么做、设计取舍、
> 踩过的 Bug 与排查过程、实测验证数据。写给人看——尤其写给 6 个月后的自己。
>
> 基线：`ca96461`（fix: 线程竞争/坏数据消费/路径依赖/anomaly标红）
> 分支终点状态：**S1~S9 全部落地** + P0 演示链路修复 + P1 可靠性补齐 + 收官批次（B10 功能码链路 / 自动停机 / 单元测试 / 拔网线实验）+ 评审清单清空（B8/B11）+ 工程化（CI/LICENSE），共 15 个提交。见第 8~8.8 节。

---

## 0. 提交史（为什么按阶段分批提交）

| 提交 | 内容 | 说明 |
|---|---|---|
| `4452b94` | 模拟器 + 通信层异步化 | 作者本人提交（S3 + S7 主体） |
| `f77279f` | S1/S2 SQLite 加固与降级 + S4 调度侧 + 文档入库 | 含一条重要修正：**提交消息经 `--amend` 改写过**（见 Bug 4） |
| `ea04cd0` | S4 门面侧 + S5 属性体系 + S6 UI 绑定 + S8 存储线程 | 见第 5/6/7 节 |
| `99cf9a9` | P0 演示链路修复（启停闭环 / rate 可触发 / 坏数据防闪 / Test Profile 接线 / timeoutMs 透传） | 见第 8 节 |
| `4682257` | README 对齐 + BRANCH_NOTES.md 初版 | 分支全记录 |
| `cf2e817` | S9 全量重启 + B1/B5/B6/B7 + 曲线双轴 | 见第 8.5 节 |
| `bc9ddbc` | README/BRANCH_NOTES 同步 S9+P1 | S1~S9 收官 |
| `f6407d5` | B10 功能码链路 + 阈值写回写 + 自动停机 + 测试最小集 + 拔网线实验 | 见第 8.6 节 |
| `ff6831d` | README/BRANCH_NOTES 同步收官批次 | 遗留减至 3 项 |
| `c18d6a7` | B8 阈值输入校验（评审最后遗留高危项） | 见第 8.7 节 |
| `ecf2694` | INTERVIEW_READINESS_REPORT 加修复状态头部 | 评审清单全部关闭 |
| `8d2b9e2` | B11 三项收尾（Logger 锁 / Metrics 摘要 / 模拟器变量名） | 见第 8.7 节 |
| `a13982f` | CI（GitHub Actions）+ MIT LICENSE + README 徽章 | 见第 8.8 节 |
| `fdaa834` | 删除异步化早期草稿 a.h/a.cpp（用户拍板不留） | 见第 8.8 节 |

按阶段分批提交：**一次提交只讲一件事，diff 可独立审查、可单独回滚**。S1/S2 是用户先写的，单独入库不与异步化混在一起。分支共 15 个提交（ca96461 基线之上），每一条的消息都自解释"改了什么、为什么、验证结果"——面试官翻提交史就能看出工作习惯。

---

## 1. 为什么要有这个分支

原项目（ca96461 基线）的三大痛点：

1. **没有从站就没有演示**——README 写"需要真实 Modbus 设备"，简历上的"控制闭环、可靠性"全成了纸面话
2. **Modbus 读写是同步阻塞**——断线时每请求 0.5s 超时 × 6 任务 × 100ms tick，调度循环被 IO 吃掉；`modbus_connect` 失败最长卡 21 秒（Windows SYN 无响应），工作线程直接冻结
3. **UI 拿不到后端状态**——状态靠 main.cpp 里 5 条带参信号直连，加一个展示位就要改一行 main

分支目标：**无硬件可演示 + 异步不阻塞 + 状态可绑定**，全部按产品级标准做（不接受只跑通的 demo）。

---

## 2. 内置模拟器（S3）——让演示不再依赖硬件

### 做了什么
`src/simulator/modbus_simulator.{h,cpp}`：一个 `QTcpServer` 从站，`-s` 启动。
- MBAP 大端解析：事务ID(2) + 协议0(2) + 长度(2) + 从站(1) + 功能码(1)
- 功能码路由：03 读保持 / 04 读输入 / 05 写单线圈 / 06 写单寄存器 / 10 写多寄存器，未知功能码回 0x01 异常
- **粘包/半包处理**：按 `length` 字段逐帧截取（`onReadyRead` 里攒 buffer 再切帧）
- **随机游走**：1 秒 tick，每寄存器带限抖动，曲线才动得起来
- **启停镜像**：写地址 0 → 镜像到地址 12（P0 时归一化为 0/1）

### 为什么这么设计（取舍）
- **用 QTcpServer 而不是 libmodbus 从站 API**：libmodbus 从站路径也要 `extern "C"` 混编，且不便于演示"MBAP 手工解析"——而手工解析恰恰是面试题本体。成本几乎一样，收益差一个量级。
- **QDataStream 默认就是大端**，与 Modbus 字节序天然一致；float 用"高字在前 + 大端"与客户端 `modbus_get_float_abcd` 对齐（`readFloatRegister` 注释里写了字节序推导）。
- **多客户端**：`m_clients` 列表管理，`stop()` 统一断开。这让"被测程序 + 外部主站脚本"可以同时连——第 8 节的闭环验证就靠这个。
- **一个真坑（源码里留了注释）**：`buildWriteResponse` 不能"先 `out <<` 6 字节再 `rsp.append` 再 `out <<`"——QDataStream 内部位置停在已写处，后续写入会**覆盖** append 进去的内容。所以写响应全程走流；读响应/异常响应用"流 + append 不再回流"。三种封包风格不完全统一，是历史遗留，被问到要能解释。

### 随机游走参数的"演示工程"
| 指标 | 游走范围 | 意图 |
|---|---|---|
| power | 6.5~9 kW | 区间刻意跨过 8.0 阈值，演示阈值告警 |
| current | ±3A/s 步长 | 放大步长让变化率规则可触发（见 Bug 6） |
| temp | 18~35°C | 常态不触发温度告警（异常采样演示见 S4 验证） |

---

## 3. 异步化（S7）——最核心的架构决策

### 最终形态（比原计划更好）
原施工计划（DEVELOPMENT_PLAN）写的是 **QtConcurrent 线程池 + QMutex 护 m_ctx**；落地改成了：

```
ModbusTcpClient（对象线程/工作线程）        ModbusIoWorker（专用 IO 线程）
├─ FIFO 队列（std::deque<PendingRequest>）  ├─ 唯一持有 m_ctx（modbus_t*）
├─ m_inflight 单飞槽位                     ├─ doConnect / performRead / performWrite
├─ LinkState 状态机                        └─ 只在这条线程碰 m_ctx
└─ 回调分派（invokeMethod 回投对象线程）
```

### 为什么放弃 QtConcurrent 方案
1. **锁是纪律，线程封闭是结构**。QtConcurrent 方案里"谁能碰 m_ctx"靠"记得加锁"；worker 方案里 m_ctx 是 worker 的成员、worker 只活在 IO 线程——**"谁能访问"和"在哪个线程访问"是同一个答案**，锁从根上消失了。
2. **队列与在飞位也零锁**：都只在对象线程访问。单飞派发的正确性由"整包搬移"保证——请求和它的回调绑死在同一个 `PendingRequest` 里一起搬，不会错位。
3. `LinkState{Disconnected, Connecting, Connected, Stopped}` 四态机解决锁解决不了的三件事：防重复连接（Connecting 占坑，杜绝连出第二个 modbus_t 泄漏）、快速失败（未连接直接失败返回，不等 2s 超时）、Stopped 终态（stop 后一切回调丢弃）。

### 关键取舍
- **回调式 API 而非返回结果**：`readRegisters(req, cb)`——返回值意味着调用方必须等；回调意味着调用方先走、结果到了被叫回来。这是"为什么需要异步回调"面试话术的实体。
- **PendingRequest 结构**（用户拍板的方案 A）：`isWrite` 标志 + 双字段（读请求体 / 写请求体）+ 回调。否决了 variant / 多态 / 闭包——双字段最直白，标签位一眼看懂。
- **connect 也异步**：`modbus_connect` 本身阻塞（最长约 2s），丢 IO 线程跑，`connectAsync(cb)` 回投。否则"异步了读写、异步了重连，唯独 connect 还卡线程"——那就只是把阻塞挪了个位置（评审报告 B2 的教训）。
- **连接级错误 vs 协议异常**：`ModbusResponse.linkBroken` 区分超时/断连（触发降级 + 清队列）与 0x01~0x03 协议异常（只记日志，连接还活着，重连只会添乱）。
- **调度器侧适配**：`PollingTask.pending` 单指标防重发（设备变慢时同一指标不会在响应回来前被反复发起，队列堆满过期请求）；`lastPollMs` 语义从"上次完成"改为"上次发起"；回调捕获 `QPointer<DeviceDriver>` 防热更新销毁驱动后悬垂。

---

## 4. SQLite 加固与降级（S1/S2）

### S1 做了什么
- 建表补 `id INTEGER PRIMARY KEY AUTOINCREMENT` + 双索引（`idx_telemetry_ts` / `idx_telemetry_name`），保留复合索引 `idx_name_ts`
- 析构 `close()` → **先释放句柄（`m_db = QSqlDatabase()`）再 `removeDatabase(m_connectionName)`**——少掉释放句柄那步，Qt 会打 "connection is still in use" 且连接没真正回收，热更新反复 addDatabase 时撞重复连接名
- 连接名从"局部变量"改为成员 `m_connectionName`

### S2 做了什么
`save()` 拆出 `tryInsert()`：3 次尝试（每次失败 `QThread::msleep(500)`），仍失败追加 `<dbPath>.cache` 文件 + warn 日志。

### 取舍
- **为什么宁可睡 1.5s 也要重试**：工业上位机"数据不能丢"优先于"采样不能停"；睡在独立存储线程上（S8 之后），不波及采集。这是 S2 与 S8 的呼应点，面试可讲。
- **为什么 AUTOINCREMENT 而不是裸 `INTEGER PRIMARY KEY`**：前者保证 ROWID 单调不回用，面试一问一个准。

---

## 5. 异常触发高频采样（S4）

### 做了什么
- 调度侧（用户写）：`PollingTask.normalIntervalMs` 记住配置间隔；`setAnomalyMode(bool)` 边沿切换所有任务 100ms ↔ 配置值；热更新重建任务时按当前异常态起步，避免"一半 1Hz 一半 10Hz"错位
- 门面侧（本次补）：`updateAnomalyMode()`——温度 >80°C 或电流 >25A（硬编码常量，见下）→ `invokeMethod` 投递 `setAnomalyMode` 到调度线程

### 取舍
- **阈值为什么硬编码 80/25 而不是进 config**：它是"采样策略"不是"业务参数"；真要配置化应是独立字段，塞进 rules[] 会跟规则语义混淆。
- **为什么用 EquipmentData 最新值判定**：6 个指标不同时刻到达，取本帧更新后的快照做判定，比"只判当前样本"稳定（不会因为恰好收到电流样本而漏判温度超标）。
- **为什么边沿触发**：状态没翻转就不打扰调度器，日志里也不会每秒刷一行。
- **为什么跨线程必须 invokeMethod**：调度器在工作线程，直接调 `setAnomalyMode` 是裸跨线程调用；投递过去才保证在调度器自己的线程执行。

---

## 6. 状态属性体系（S5）+ UI 绑定（S6）

### S5 做了什么
ServiceFacade 暴露 25 个 `Q_PROPERTY` + **无参 NOTIFY** 信号：serviceState 三态（online/degraded/offline）、连接/调度/采样龄、遥测计数、规则名/触发数/最近 4 条事件、端点、重连消息、启停状态与消息、4 项阈值快照、写消息、评估计数/规则数、倒计时、判定。

### 关键设计决策
1. **为什么信号无参**：属性系统的标准形态是 `Q_PROPERTY(READ, NOTIFY)`——UI 收到 NOTIFY 后用 getter 读值。信号夹参数就出现"信号带参 + 属性"双通道，两处都要维护。破坏性迁移连带 main.cpp 5 条直连一起改掉。
2. **refreshUiState 全边沿检测**：1s 定时器统一刷新，**只有值变了才发信号**（review 报告 B11 指出旧代码 `healthyChanged` 每秒无条件 emit——同函数两种风格）。
3. **serviceState 的 `age >= 0` 守卫**：`lastSampleAgeMs` 在无采样史时返回 -1，`-1 < 10000` 恒真——不加守卫会出现"刚连上还没数据就亮绿"（文档原稿的笔误，落地时修正）。
4. **阈值快照"写前即更"**：写回调跑在工作线程，跨线程改主线程成员是数据竞争。S5 阶段选"写前即更 + 失败只记日志"（UI 立即反馈），"写成功才回写"留待 S7 后处理（见遗留 9）。
5. **UI 绑定晚于 start() 必须补帧**：`facade.start()` 里的信号（configure 归零、首帧状态）在 `bindServiceFacade` 之前就发完了——连接在 start 之后，注定错过。所以绑定末尾手动补一帧初始状态，否则 UI 空白。
6. **渲染逻辑抽成 `apply*` 私有函数**：信号 lambda 与初始补帧共用同一份渲染代码，避免"同一逻辑写两遍、改一处漏一处"。
7. **B9 顺手修复**：`configure()` 的 `rebuildTasks` 从"直接调"改为 `invokeMethod`——对象未 moveToThread 时直接执行、已搬家时自动变队列连接，**同一段代码两条路径都安全**（原来"start 前调用碰巧安全"是脆弱性）。

### S6 做了什么
`MainWindow::setServiceFacade` 传递者模式（与 `setSensor` 同款）；HomePage 新增规则状态卡（触发变红 ×N）、SettingsPage 新增连接/调度状态、阈值快照组、规则消息、写消息。

---

## 7. 存储线程分离（S8）

- `ServiceFacade` 增加 `m_dbThread`；`SqliteRepository` 独立搬过去
- stop 顺序：先 `BlockingQueuedConnection` 停 Session/Scheduler → `quit()+wait()` 两线程 → **`SqliteRepository` 的析构以 `BlockingQueuedConnection` 投递到存储线程执行**

### 为什么析构必须回存储线程
SQLite 连接在哪个线程创建，就应在哪个线程回收。析构里 `close + removeDatabase` 若在主线程执行（stop 里直接 `delete`），就是跨线程回收连接——可能出 "still in use" 类问题。阻塞投递保证"真正删完了才继续"。

### 顺手修复
- **B4**：`stop()` 原来不 delete `healthMonitor`、指针不置空——`start()` 第二次会重复连接信号（一条样本进两次 onSampleReady、计数翻倍）。现在析构 + 置空。
- **S1 防御**：`removeDatabase` 前判 `!m_connectionName.isEmpty()`——start 后立刻 stop 时队列里的 open() 会被丢弃，连接名是空串，`removeDatabase("")` 会误伤默认连接。

---

## 8. P0 演示链路修复（本分支收尾）

### P0-1 启停闭环可见
- **模拟器**：`mirrorStartStop` 归一化——写 0xFF00 → 寄存器 12 = 1，写 0 → 0
- **config.json**：`items[]` 加第 7 项 `runStatus`（地址 12，scale 1.0）
- **EquipmentData**：新增 `runStatus` 属性（int）
- **HomePage**：状态栏加"运行: 是/否"标签

**取舍**：归一化在模拟器做而不是 UI 用 scale 0.0000153 硬凑（评审报告 D1 的备选方案）——65280 是**协议层语义**，1/0 才是**业务语义**，脏活不该留给显示层。

### P0-2 变化率规则可触发
config `current_rate.rateLimit` 8.0 → **2.5**；模拟器电流步长 200 → **300 raw（±3A/s）**。

**调参逻辑**：步长均匀分布 ±3A，rate > 2.5 的概率 ≈ (300−250)/300 ≈ 17%/秒 → 约每 6 秒一次告警。评审报告建议的 1.5 会导致 50%/秒的告警风暴（每 2 秒红一行），演示效果反而差，故取 2.5。

### P0-3 坏数据不刷实时卡片
`onSampleReady` 对 `setXxx` 系列加 `quality == "good"` 门。**注意是"只挡卡片"不是 return**——落库 / 表格标红 / 计数这些质量标记链路必须保留（bad 数据不静默丢弃是 README 语义）。评审报告原话"开头 if bad return"会连落库一起杀掉，是错的。

### P0-4 设置页 Test Profile 接线 + Connect 接线
- 5 个匿名 QLineEdit 改成员 + Apply 按钮 → `ServiceFacade::applyTestProfile()`：只 `setProfile` + 发一帧 `remainingSecondsChanged`，**不 stop/start TestRunner**——在跑的测试按新时长继续算倒计时（`remainingSeconds` 基于 profile 实时算），峰值历史保留。演示点：72h 改 0.02h → 倒计时立刻变 ~1 分钟。
- `connectRequested`（死信号）接上 `ServiceFacade::onConnectRequested()`：**先 stop 旧会话（连带停退避重连定时器，避免"重连到旧地址"与"连新地址"打架）再 start 新端点**，两个动作按序投递到会话线程。
- 端口默认 1502 → 502（对齐 config）。

### P0-5 timeoutMs 透传
`ModbusSession::start(host, port)` → 加 `timeoutMs` 参数 → `m_client.configure(host, port, timeoutMs)`。之前 `endpoint.timeoutMs` 解析了但从未使用（client 一直 2000ms 默认），改配置无效。

---

## 8.5 P1/S9 批次：热更新全量重启 + 可靠性补齐

### S9 热更新全量重启（最后一块 S 拼图）
- **configure() 变为"全量装配"**：规则重建 + 状态归零 + `m_itemNames` 记录 + **组件幂等重建**（stop 后指针全空，configure 里补建）+ moveToThread + 线程 start（幂等）
- **start() 只做启动动作**：configure → 投递 open/session.start/scheduler.start → healthMonitor → TestRunner → uiTimer
- **onConfigUpdated = `stop() + start(config)`**：旧组件/线程/信号全部回收再重建，不泄漏、不重复 connect
- **stop() 顺带修了一个真死锁**（见 Bug 9）

### B1 热更新配置校验
`ConfigWatcher::onFileChanged`：`loadFromFile` 后校验 `items`/`host`，无效 → warn + **不 emit**（保持旧配置运行）+ 重新挂 watch（编辑器保存可能删重建文件）。
实测：坏 JSON → `Config reload rejected (invalid config), keeping current config running`，进程存活、采集继续。

### B5 表格增量刷新
`onHistoryUpdated` 每帧只处理新增行：`datas.size() == rowCount`（50 行 cap 稳态）→ `removeRow(0)` + 补末尾 1 行；异常收缩才全量重建。每秒 350 次 item 分配 → 7 次。

### B6 帧聚合按 schema 核对 + 超时兜底
`onSampleReady` 改为：按 `m_itemNames`（config items 名单）逐项 `contains` 核对，攒齐才推；`QElapsedTimer` 1.5s 攒不齐强制推（缺项留空）。修复"数 key 个数"的两宗罪：丢采样时旧值残留串帧、items < 6 时表格永久冻结。

### B7 TestRunner 三修
- `recordSample(name, value, good)`：bad 不入峰值（解码失败值 0 会污染 qMax，误判 FAIL）
- `abort()`：人为中止（关机/热更新）判 `aborted`，不算 pass/fail——中途停止判 PASS 是"把没测完当合格"的谎言
- `remainingSeconds()`：`testDurationMinutes > 0` 时优先（分钟级），设置页 Duration 支持小数小时（≥1 → hours，<1 → 折算 minutes）
- 首页 verdict 增加灰色 ABORTED 显示

### A2 曲线双 Y 轴
QCustomPlot 第二条曲线挂 `yAxis2`（右轴，电流 0~30A），温度左轴 20~100°C。面试话术：双 Y 轴量纲隔离。

---

## 8.6 收官批次：B10 功能码链路 + 阈值写回写 + 自动停机 + 测试集 + 拔网线

### B10 四类功能码读链路（"假支持"变真支持）
- `ModbusDeviceDriver` 加 `setRegisterType/getRegisterType`；三个具体驱动的 `buildReadRequest` 不再写死 HoldingRegister
- `PollingScheduler::rebuildTasks` 把 config `items[].registerType` 经 `registerTypeFromString` 传给驱动
- `ModbusIoWorker::performRead` 按类型分发：01 `modbus_read_bits` / 02 `modbus_read_input_bits` / 03 `modbus_read_registers` / 04 `modbus_read_input_registers`
- **坑**：bit 系列（01/02）的 dest 是 `uint8_t*`，寄存器系列（03/04）是 `uint16_t*`——必须分开缓冲区（第一次编译就报类型错误）
- **实测**：临时把 temperature 改 `registerType: "input"` → 采样 6 条、值 0（模拟器 input 寄存器空）、quality 全 good、无错误日志 → 04 链路真通
- 诚实边界：01/02 链路已通，但驱动解码层只服务寄存器格式（bit 采集项标 bad）

### 阈值写成功才回写快照（P2 遗留 2 号）
- 6 个写回调都跑在**工作线程且串行**（client 单飞），用 `std::shared_ptr<QVector<bool>>` 共享数组统计结果无竞争
- 全部收齐后 `invokeMethod(this, ..., QueuedConnection)` **回主线程**更新快照——跨线程改主线程成员必须投递
- 全成功 → 快照 + "updated at HH:MM:SS"；任一失败 → "Threshold write failed, snapshot kept"（保留旧快照，不撒谎）
- **顺带修了 S5 的一个缺口**：`setThresholdSnapshot` 原来漏设 `m_powerThreshold`，设置页 Power 快照永远 "-"

### 规则自动停机联动（P2 遗留 3 号）
- `RuleConfig.autoStop`（config 解析）+ `configure` 收集 `m_autoStopRules`
- ruleTriggered lambda：`m_isRunning && m_autoStopRules.contains(name)` → 翻转状态 + `writeMotorStop()`（写停机线圈）
- **m_isRunning 守卫**：规则每秒触发时只停机一次，不刷屏
- **实测**：临时 main.cpp 3 秒后模拟点启动（验证后删除）→ temp_high(22) 触发 → `Auto-stop triggered by rule: temp_high` 只出现一次 → runStatus 分布 0×10/1×1（启动回读 1、停机回读 0）——**"启动→回读→超限→自动停机→回读"完整闭环**
- 配置默认：temp_high / current_high 开 autoStop（安全规则），power/vib 只告警

### 单元测试最小集（Qt Test）—— ⚠️ 已于 2026-09-01 按用户决定移除

> **移除说明**：测试与 CI 均为 AI 实现。用户判断"不是自己写的不该留在求职作品里"，故将 `tests/`、`unit_tests` CMake target、`.github/workflows/ci.yml` 一并删除。
> 本节保留作为**历史记录**——它证明过一件重要的事：测试抓到了 Bug 11（RateChangeRule 首帧哨兵）。以后若重新引入自动化测试，本节是现成的设计与踩坑清单。
> 恢复方法：`git checkout fdaa834 -- tests .github CMakeLists.txt`（该提交含完整测试与 CI 配置）。

- `tests/tst_main.cpp`：7 组用例 9 断言——阈值/变化率规则、引擎计数与信号、TestRunner 时长/bad 过滤/abort、ConfigLoader 解析（timeoutMs/registerType/autoStop）
- CMake：`unit_tests` target + `enable_testing()/add_test()`，`ctest` 可跑
- **坑 1**：测试 target 的头文件必须显式列出，否则 AUTOMOC 漏掉 Q_OBJECT 基类（Rule）的 moc → 链接 undefined reference（主程序列了所有 .h 所以没踩过）
- **坑 2**：bit 读取 dest 类型不同（见上）
- **坑 3**：Windows 上 unit_tests.exe 直接跑退出码 127 = 缺 `Qt6Test.dll`，要把 Qt bin 加 PATH；Git Bash 管道吞 Qt console 输出（PowerShell 才能捕获）
- **最大收获**：测试抓到一个真 bug（见 Bug 11）

### 拔网线实验（timeoutMs 分层验收）
用 Python 静默服务器（accept 但不响应）模拟"设备挂死"，实测时间线：
```
connect success → (2s 响应超时, timeoutMs=2000 生效) → read failed →
断连感知 → Reconnect scheduled 1000ms → Attempt + connect success（TCP 通）→ 又超时……
```
- **app 全程存活、UI 不卡**（异步化的直接收益，量化证据）
- **退避一直是 1000ms 而非增长**：TCP connect 每次都成功（服务器在监听），退避只在 connect 失败才翻倍——**"响应超时"与"连接退避"是两个独立分层**，行为符合设计

---

## 8.7 评审收尾批次：B8 + B11（评审清单全部关闭）

### B8 阈值输入校验
设置页 Save 阈值：空输入 `toDouble()=0` → 温度阈值写成 0 → isUpper 规则恒真 → **告警风暴**。修法：解析失败拒绝发送 + `qBound(1.0, v, 100000)` 钳位 + 写消息提示。这是评审报告里最后一个 A/B 级未修项。

### B11 三项
- **Logger 级别锁外读**：`emitLine` 的 `if (level < m_level)` 在锁外——setLevel（带锁）并发时是 TSAN 级竞争（ca96461 修复的漏网之鱼）。级别判断挪进锁内。
- **MetricsCollector 只写不读**：接 60s 摘要日志消费者（refreshUiState 计数 60 次触发）。实测输出：`Metrics summary: vibration[min=2.00 max=4.41 avg=3.00 n=55] current[...] ...` 7 指标全量。
- **模拟器 WriteSingleRegister 变量名**：`quantity` 改名 `value`——0x06 请求里该字段语义是"要写的寄存器值"，不是数量。纯语义修正。

## 8.8 工程化收尾：CI + LICENSE + 删除草稿

- **CI**（`.github/workflows/ci.yml`）：Windows + Qt 6.10（win64_msvc2022_64）+ VS 2022 生成器，`-DCMAKE_PREFIX_PATH` 覆盖 CMakeLists 硬编码的本机 Qt 路径（`E:/Qt/6.10.0/mingw_64`），构建后 `ctest -C Release`（9 用例）。触发：push main/feat 分支 + PR。
  **⚠️ 已于 2026-09-01 随测试一并移除**（用户决定：AI 实现的测试/CI 不留在求职作品里）。配置未经过实测验证。
  **设计要点（若日后重配可参考）**：C++ 没有标准 ABI，Qt 库按编译器预编译（mingw 版 Qt 只能用 MinGW、msvc 版只能用 MSVC），所以 CI 选 MSVC 套件与本地 MinGW 不同——这反而多一层"代码在第二个编译器下也能编译"的可移植性验证。上位机 CI 的正确姿势是只跑**构建 + 纯逻辑测试**，不要试图跑 GUI。
- **LICENSE**：MIT（Copyright 2026 msrnnnnn）。
- **删除 `src/modbus/a.h/a.cpp`**：异步化早期接口草稿（与现行 modbustcpclient 定义同名类），代码零引用，用户拍板不留。同步清理 CMakeLists 注释、README、遗留清单。

---

## 9. Bug 记录与排查（按时间线）

### Bug 1：`std::vector` 没有 `isEmpty()`（编译错误）
**现象**：`PollingScheduler::setAnomalyMode` 编译失败：`'class std::vector<PollingTask>' has no member named 'isEmpty'`。
**根因**：`isEmpty()` 是 Qt 容器 API；`std::vector` 是 `empty()`。用户代码用了 Qt 习惯写标准容器。
**排查**：编译器直接指到 `m_tasks.isEmpty()` 一行，无歧义。
**修法**：`empty()`。教训：**Qt 容器（isEmpty/contains/size）与 STL（empty/find/size）API 混用是高频编译错**，写完先扫一遍容器调用。

### Bug 2：S4 验证 Anomaly 日志为 0 —— 误判"功能没生效"（本次排查最值钱的一段）
**现象**：把模拟器温度游走上限临时调到 90°C，跑 14 秒，日志里规则触发 7 次，但 `Anomaly sampling ON` 一次都没有。
**第一步排查（缩小范围）**：先看日志确认那 7 次规则触发**全是 power_high**（8.0 阈值），不是 temp_high——说明温度根本没到 85+。
**第二步（查数据）**：查 DB 温度 max = 26.76°C，而上次（上限 35）是 26.28——**两个几乎一样**，说明温度游走根本没走到上限。
**第三步（数学）**：游走步长 ±1.0°C/秒，随机游走的期望位移是 σ√n（n=14 步 → 约 ±3.7°C），从 18 起步 14 秒最多爬到 ~26，**要到 80 需要 60+ 步**。步长太小，不是逻辑问题，是验证时长/参数问题。
**验证修正**：种子值直接设 80.0 + 游走范围 70~95 → 日志立即出现 `Anomaly sampling ON: interval -> 100ms` / `OFF -> 1000ms` 边沿切换。**功能没问题**。
**教训**：验证一个"阈值触发"型功能，先确认输入真的越过了阈值——**别拿"日志没有 X"当"X 没实现"，先查"X 的输入条件是否成立"**。这条排查链本身就是面试故事。

### Bug 3：exe 被占用（Permission denied）+ Git Bash 的 taskkill 坑
**现象**：连续两次 `cmake --build` 在链接阶段失败：`cannot open output file EquipmentBurnInMonitor.exe: Permission denied`。
**根因**：前一轮冒烟测试的 GUI 进程还活着（`taskkill //F` 在 Git Bash 里把 `//F` 转义成了非法参数，进程没被杀掉）。
**排查**：`tasklist | grep -i EquipmentBurn` 确认 PID 8872 存活。
**修法**：改用 PowerShell `Get-Process EquipmentBurnInMonitor | Stop-Process -Force`（规避 MSYS 路径转义）。**之后一律用 PowerShell 杀 Windows GUI 进程**。

### Bug 4：提交消息与内容不符（历史真实性）
**现象**：第一次 `git add -A` 把用户未提交的 S1/S2 代码也带进来了，但提交消息写的是"模拟器+异步化"——**消息描述的其实是上一个提交（4452b94）的内容**。
**处理**：`git commit --amend` 改写消息为真实内容。教训：**`git add -A` 前必须 `git status` 核对暂存清单**——"commit 前先看将要提交什么"，而不是提交完再看。

### Bug 5：A4 坏数据闪 0（评审发现，本次修复）
**现象**：断线时解码失败返回 `value=0, quality="bad"`，`onSampleReady` 不判质量直接 `setTemperature(0)` → 6 张卡片集体闪 0，恢复后跳回。
**修法**：见 P0-3。面试话术：**bad 的语义是"不可信"，UI 不能把它当 0 显示**。

### Bug 6：变化率规则永不触发（评审发现，本次修复）
**现象**：`current_rate` rateLimit 8.0，模拟器电流步长 ±2A/s——两帧差值的 rate 数学上最大 2 < 8，规则永久沉睡。`RateChangeRule` 是简历上"变化率突变检测"的实体，演示不了等于这条亮点不存在。
**修法**：调参（见 P0-2）。同时承认两帧差值算法本身脆弱，窗口算法是正解（遗留项）。

### Bug 7：timeoutMs 是死字段（评审发现 B2，本次修复）
**现象**：`config.cpp` 解析了 `endpoint.timeoutMs`，全工程无人使用；libmodbus 默认响应超时 0.5s 与 connect 阻塞 21s 并存，故障场景行为不可控。
**修法**：P0-5 透传。**验收标准**：改 config 的 timeoutMs → `modbus_set_response_timeout` 实际生效（连接路径验证见遗留）。

### Bug 8：README 文档债（系统性）
**现象**：README 停留在异步化之前：写着"Modbus 同步阻塞"、"模拟器在路线图中"、"需要真实设备才能看数据"——与实际代码完全脱节；且从未纳入 git（untracked）。
**处理**：两轮重写（异步化落地后、S 线 + P0 落地后），并纳入版本控制。
**教训**：**文档是代码的投影，代码变了文档必须跟着变**；文档不进 git 等于没有文档。

### Bug 9：S8 的 stop() 藏着死锁——quit 之后再 BlockingQueued（做 S9 时重读代码发现）
**现象**：S9 改造前重读 stop()，发现 sqlite 析构的顺序是：
```cpp
m_dbThread->quit(); m_dbThread->wait();
QMetaObject::invokeMethod(m_sqliteRepository, [..]{ delete ..; }, Qt::BlockingQueuedConnection);
```
**根因**：`quit()+wait()` 之后 db 线程的事件循环已经退出，`BlockingQueuedConnection` 的事件永远等不到处理 → **永久阻塞（死锁）**。之前的冒烟测试全是 `taskkill` 强杀进程，优雅关闭路径（析构 → stop）从没被走到，所以没暴露。
**修法**：把删除投递到 `quit()` **之前**——delete 排在队列末尾，天然等所有排队的 save() 处理完，再 quit/wait。
**教训**：**GUI 程序的"优雅退出路径"必须真实验证**，强杀进程测不出关闭逻辑的问题。S9 的全量重启恰好把这个路径变成了热路径（每次改配置都走一遍），等于免费测了 20 遍 stop()。

### Bug 10：`applyTestProfile` 的小数时长被 `toInt()` 吃掉
**现象**：P0 接线时设置页 Duration 用 `text().toInt()`——演示输入 0.02 得到 0，倒计时直接归零判 FAIL。
**修法**：改 `toDouble()`，≥1 小时走 `testDurationHours`，<1 小时折算 `testDurationMinutes`（B7 的分钟级字段正好用上）。
**教训**：**输入解析要按输入域的语义选类型**——用户能敲小数的框就别用 toInt。

### Bug 11：RateChangeRule 首帧判断——时间戳为 0 时永远算不出 rate（单元测试抓到）
**现象**：单元测试 `rateTrigger` 失败：`evaluate(b).triggered` 返回 FALSE。
**根因**：首帧判断用 `if (m_lastTimestamp == 0)`，但首帧的 `timestampMs` 恰好是 0（测试/回放数据）→ 第二次评估仍命中"首帧"分支 → 只记录不计算。真实场景时间戳是 epoch 毫秒（永不为 0），所以这个 bug 从没在生产路径暴露——**测试用构造数据把它炸出来了**。
**修法**：加独立 `m_hasBaseline` 标志，不再用时间戳当哨兵。
**教训**：**"是否首帧"这类状态不能用数据本身当哨兵**，数据范围可能覆盖哨兵值；测试的价值恰恰在于构造"生产里不会出现的输入"。

### Bug 12：AUTOMOC 对 Q_OBJECT 基类的 moc 依赖头文件列出
**现象**：`unit_tests` 链接失败：`undefined reference to Rule::qt_metacast/staticMetaObject/vtable`。
**根因**：测试 target 的源码列表只列了 .cpp；AUTOMOC 只 moc 了派生类（thresholdrule 等），Q_OBJECT 基类 Rule.h 没进 target 源码列表就没生成 moc。主程序没踩过是因为 PROJECT_SOURCES 显式列了全部 .h。
**修法**：TEST_SOURCES 补列全部相关头文件。
**教训**：**新建 CMake target 时，含 Q_OBJECT 的头文件要显式列入源码列表**，别只列 .cpp。

### Bug 13：Windows 上 Qt Test 的两连坑（运行环境）
**现象**：① `unit_tests.exe` 直接跑退出码 127（缺 Qt6Test.dll，Qt bin 不在 PATH）；② Git Bash 管道/重定向吞掉了 Qt console 输出（PowerShell 才能捕获）。
**处理**：加 PATH 跑；用 PowerShell 捕获输出。
**教训**：**Windows 下测 Qt 程序，环境变量与 shell 差异会先于业务问题找上门**——先确认 DLL 路径与输出通道，再谈测试结果。

---

## 10. 实测验证记录（数字全部来自本次运行）

| 验证项 | 方法 | 结果 |
|---|---|---|
| 编译 | `cmake --build build/MinGW_14-Debug`（Ninja + MinGW 13.1） | 全绿 |
| 基础链路 | `-s` 跑 10s | connect success、90 行落库（6 指标×15）、无崩溃 |
| S1 | `PRAGMA table_info` / `sqlite_master` | id 主键在、三索引在 |
| S8 | 三线程运行 10s | 采样与落库均衡（6 指标各 15 条） |
| S4 | 温度种子 80 + 游走 70~95 | `Anomaly sampling ON/OFF` 边沿切换日志 ✓ |
| P0-1 闭环 | 外部 Python 主站写线圈 0 → 读寄存器 12 | 写 ON→1、写 OFF→0，归一化正确；被测程序 ON 期间采到 3 条 value=1 落库 |
| P0-2 | 日志 grep `current_rate` | `Rule triggered: current_rate - current 6.66 >= 2.5` ✓（原永不触发） |
| **S9 全量重启** | 运行中改 config（power_high 8.0→9.5） | `full reload (stop -> start)` + 再次 `connect success`，**无死锁无崩溃** |
| **B1 拒绝坏配置** | 写坏 JSON 到 config.json | `Config reload rejected ... keeping current config running`，进程存活、采集继续 |
| S9 恢复 | 恢复 config 后再跑 | 采样连续（约 20s × 7 指标 = 140 行落库） |
| **单元测试** | `unit_tests.exe`（Qt Test，Qt bin 加 PATH） | **9/9 全绿**；首版 8/9，抓到 Bug 11 |
| **B10 04 链路** | temperature 临时改 `registerType: input` | 采样 6 条、值 0、quality 全 good、无错误日志 |
| **自动停机** | temp_high 阈值临时降 22 + 3 秒模拟点启动 | `Auto-stop triggered by rule: temp_high` 仅 1 次；runStatus 0×10/1×1（启动回读 1、停机回读 0） |
| **拔网线** | Python 静默服务器（accept 不响应） | 2s 响应超时 → 断连感知 → 1s 重连循环；**app 全程存活**；退避不增长（TCP connect 成功），符合"响应超时/连接退避分层"设计 |

---

## 11. 遗留事项（未做，需决策）

1. ~~`a.h/a.cpp` 异步草稿的去留~~ —— **已删除（2026-09-01，用户拍板"不留"）**：代码零引用，确认不再需要
2. ~~自动化测试~~ —— **已移除（2026-09-01 用户决定）**：原 9 用例（纯逻辑）+ CI 均删除；恢复见 §8.6 移除说明
3. 01/02（bit）功能码链路已通但驱动解码只支持寄存器格式——若要支持 bit 采集项需配套解码器（当前标 bad 属诚实失败）
4. GUI 截图/演示视频（由用户本地截图，未纳入分支）

---

*维护约定：代码行为变化后，同步更新本文档与 README；所有数字必须来自实测，不引用文档自述。*

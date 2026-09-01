# EquipmentBurnInMonitor 源码深度评审与校招补强报告（v2）

> 评审日期：2026-08-23 ｜ 基线：`feat/simulator-async`（ca96461 + 未提交的 S3 模拟器）
> 本版基于**全部自有源码逐文件通读**（约 3,400 行：Facade/Session/Client/Scheduler/驱动/规则/UI/存储/诊断/配置/日志/模拟器），每条发现附 `文件:行号` 证据，取代 v1 的清单式评估。
> 组织方式：按**面试官发现问题的速度**分级——A 类是打开软件 30 秒内肉眼可见的，B 类是追问"失败了怎么办"3 分钟内露馅的，C 类是深挖架构 10 分钟才会碰到的。暴露得越快，杀伤力越大，优先级越高。

---

## 总体判断

架构骨架（分层、门面、驱动抽象、质量标记贯穿、线程模型）是真实做对了的，`ca96461` 的线程竞争修复也确实抓到了要害（`m_connected` 改 `std::atomic<bool>`）。但通读后发现：**这个项目当前最大的风险不是"功能不够"，而是三条"看起来通了、实际断着"的链路**（模拟器镜像无人消费、rate 规则参数上永不触发、InputRegister 假支持），以及一组**只在故障场景才暴露的可靠性缺口**（热更新无校验、超时配置是死字段、断线 10 秒盲区）。这些恰好是面试官最爱问的"边界与异常"区域。

另一个结论：**上一轮施工计划（DEVELOPMENT_PLAN S1-S9）的优先级需要微调**——S5/S6（Q_PROPERTY 体系）之前，有若干 30 分钟级的小修性价比更高，详见 D 节。

---

## A 类：打开软件 30 秒内就能看到的问题（演示现场杀伤力最高）

### A1 设置页 Test Profile 整组是死 UI
**证据**：`pages/settingspage.cpp:55-59` —— Duration / Rated RPM / Rated Load / Max Temp / Max Vibration 五个输入框创建了但**没有任何绑定**，连保存按钮都没有。同页 Connect 按钮（`settingspage.cpp:92-96`）发出的 `connectRequested` 无人消费（已知问题）。设置页三组：Thresholds ✓ 能用、Mode ✓ 能用、Connection 半死（输入框+死按钮）、Test Profile 全死。
**为什么致命**：演示点开 Settings 页，面试官看到 5 个不能用的输入框——"这是没写完吗？"比没有这个功能更减分。
**修法**（二选一，0.5 小时）：① 删掉 Test Profile 组（最诚实）；② 绑定到 `facade.configure()` 走热更新链路（反而多一个演示点：改 Duration → 倒计时变化）。Connect 按钮接 `ModbusSession::reconnect` 或先隐藏。

### A2 曲线只画温度一条，Y 轴写死 20~100°C
**证据**：`pages/homepage.cpp:129-134` —— 只 `addGraph()` 一次，`yAxis->setRange(20, 100)` 硬编码；模拟器温度游走 18~35°C，曲线永远只占画面下半截。
**为什么致命**：大屏演示时最容易被问"其他 5 个指标呢？"。工业大屏的标配是多曲线对比（温度 vs 电流双 Y 轴）。
**修法**（1~2 小时）：加第二条曲线（current，右轴）+ `rescaleAxes` 或动态范围；面试话术：双 Y 轴量纲隔离（QCustomPlot `yAxis2`）。

### A3 "启停控制闭环"演示链路断在最后一环
**证据**：`src/simulator/modbus_simulator.cpp:285-288` —— `mirrorStartStop` 把启停命令镜像到寄存器 12（设计意图：读 12 可见运行状态）；但 `config.json` 的 `items[]` 只采集 5/7/8/9/10/11，**没有任何任务读地址 12**，镜像结果无 UI 消费。且镜像值是 `0xFF00`=65280，即使读了，scale=1 时显示 65280 也很难看。
**为什么致命**：简历和 README 都写了"控制闭环"——但演示点启动按钮后，唯一的可见反馈是按钮文字自己变（`onMotorCommand` 的乐观翻转），**设备侧是否真的收到并执行，界面上无任何证据**。面试官问"你怎么确认设备真的启动了"就露馅。
**修法**（0.5 小时）：`config.json` 加第 7 项 `{ "name": "runStatus", "address": 12, "scale": 0.0000153 }`（65280×scale≈1）——或更干净：镜像逻辑改写 0/1，加采集项 scale=1。演示效果：点启动 → runStatus 卡片 0→1，**闭环当场可见**。

### A4 断线/坏数据时 6 张卡片集体闪 0
**证据**：`src/api/ServiceFacade.cpp:119-124` —— `onSampleReady` 对 `sample.quality` 不加判断直接 `setTemperature(sample.value)`；而驱动的 `decode` 在 `!rsp.success` 时返回 `value=0.0, quality="bad"`（`motortemperaturedevice.h:33-37`）。断线期间每秒 6 张卡片全部跳 "0.0"，恢复后再跳回。
**为什么致命**：演示断线重连（这是你简历"可靠性"那条的场景）时，屏幕上一排 0 跳来跳去——工业软件的常识是**坏数据显示 `--` 或保持上一有效值**，跳 0 是"数据有效"的谎言。
**修法**（0.5 小时）：`onSampleReady` 开头 `if (sample.quality != "good") return;`——坏样本仍走落库/表格标红（质量标记链路保留），但不刷实时卡片。这一改动本身就是一条面试话术："bad 数据的语义是'不可信'，UI 不能把它当 0 显示"。

### A5 变化率规则在当前参数下永远不会触发
**证据**：`config.json` 中 `current_rate` 的 `rateLimit: 8.0`（A/s）；模拟器电流游走 `stepRange=200 raw`（`main.cpp:59`）= ±2 A/s。两帧差值的 rate 数学上最大 2 A/s < 8 A/s，**该规则永远沉睡**。
**为什么致命**：`RateChangeRule` 是简历上"变化率突变检测"的实体。面试官说"演示一下变化率告警"——触发不了，等于这条亮点无法演示。
**修法**（10 分钟）：`main.cpp` 的 autoWalk 配置加一个"每 30 秒电流阶跃 ±5A"的演示序列，或把 `rateLimit` 调到 1.5 并把 `stepRange` 调到 300（±3A/s）。**注意**：这暴露了两帧差值算法的脆弱性——真实场景电流从 12A 瞬间掉到 8A（4A/s）就该告警，窗口算法（DEVELOPMENT_PLAN 已列）才是正解，此处先保证能演示。

### A6 设置页端口默认值与实际运行配置不一致
**证据**：`settingspage.cpp:21` 端口默认 `"1502"`，`config.json` 是 `502`；Host/Port 输入框与实际生效的 endpoint 无任何同步。配合 A1 的死 Connect 按钮，用户改了 Host 点 Connect 毫无反应。
**修法**：随 A1 一起处理（接线或删除）。

---

## B 类：追问"失败了怎么办"3 分钟内露馅的问题（可靠性主战场）

### B1 热更新没有配置校验：改坏 JSON 会永久杀死采集
**证据链**：`src/config/ConfigWatcher.cpp:17-18` —— `loadFromFile` 失败时返回**默认空 Config** 并照发 `updated(cfg)`；`ServiceFacade::onConfigUpdated`（`ServiceFacade.cpp:176-180`）的顺序是 **stop → rebuildTasks → start**；而 `PollingScheduler::rebuildTasks`（`PollingScheduler.cpp:38-40`）遇到 `items.isEmpty()` **直接 return（不执行，但也已经被 stop 了）**。结果：JSON 少个逗号 → 规则清空 + 采集停止 + **永不自愈**，必须重启程序。
**为什么致命**：这是"配置热更新"功能的自我否定——配置改错了不是拒绝新配置保持旧配置，而是把系统杀死。任何写过线上系统的人都会抓住这条。
**修法**（1 小时，两个层次）：① `ConfigWatcher::onFileChanged` 校验 `cfg.items.isEmpty() || host.isEmpty()` 则 `warn + 不 emit`（拒绝坏配置，保持旧配置运行）；② `onConfigUpdated` 改为"先 rebuild 后 start"或失败时回滚。**面试话术现成**："热更新的第一原则是新配置验证通过才切换，否则保持旧配置——我吃过 stop 在前、rebuild 中途 abort 的亏"。

### B2 超时配置是死字段，重连可能卡死工作线程 ~21 秒
**证据链**：`config.cpp:53` 解析了 `endpoint.timeoutMs`，但 **全工程无一处使用**（`ModbusSession::start` 只传 host/port，`ModbusTcpClient::open` 从不调 `modbus_set_response_timeout`）。libmodbus 默认值：`_RESPONSE_TIMEOUT = 500000µs = 0.5s`（`modbus-private.h:41`）。更严重的是 `modbus_connect` 是**阻塞 connect**，Windows 上 SYN 无响应默认约 21 秒——`attemptReconnect`（`modbussession.cpp:77-92`）在工作线程跑 `m_client.open()`，**目标主机不可达期间，采集 tick、写请求、下一次重连全部排在后面等 21 秒**。
**为什么致命**：面试官问"设备断电了会怎样"——优雅断开（EOF）快速失败没问题；但**网线拔了/IP 不通**就是每秒 6 次读 × 0.5s 超时挤爆 tick + 每次 重连尝试冻结线程 21s。这是"同步阻塞 + 无超时控制"的复合后果，恰好是你 S7 要解决的问题的**最完整实例**。
**修法**：短期 10 分钟——`open()` 里 `modbus_set_response_timeout(ctx, 0, config.timeoutMs*1000)`（把死字段救活）；彻底解——S7 时 connect 也进队列/线程池。**这条建议写进 S7 的验收标准**，否则异步化只是把阻塞挪了位置，故障场景行为没变。

### B3 读失败不更新连接状态，断线有 10 秒盲区
**证据链**：`ModbusSession::send`（`modbussession.cpp:40-49`）读失败只返回 `rsp.success=false`，**不清 `m_connected`、不触发重连**；坏样本照常 emit，直到 `HealthMonitor::check`（10s 无 good 样本）才 `degraded → reconnect`。也就是说断线的自愈路径 = 10 秒健康监测兜底，**通信层自己毫无知觉**。三个写调用（`ServiceFacade.cpp:166/199/211`）全部 fire-and-forget，写失败连日志都没有——"启动电机"按钮点了但写失败，UI 已经显示"运行中"（乐观翻转，`ServiceFacade.cpp:189-198`），且无人纠正。
**修法**：S7 的回调机制天然解决（读/写失败在回调里 `m_connected=false + scheduleReconnect` + 写结果回 UI）。短期可先在 `send` 失败时置 `m_connected=false`。**面试话术**：这正好构成"为什么需要异步回调"的论证——同步 API 的返回值没人消费，等于没有故障反馈通道。

### B4 `start()` 非幂等 + `stop()` 残留：热更新全量重启（S9）会被这两处卡死
**证据链**：`ServiceFacade::start`（`ServiceFacade.cpp:60-92`）无条件 `new QThread/new ModbusSession/new PollingScheduler/new SqliteRepository/new HealthMonitor`，且 `connect` 5 条信号；`stop`（`94-109`）不 delete `m_healthMonitor`、不 delete `m_workThread`（`QThread(this)` 父对象持有）、指针不置空。调用两次 `start()` = 对象泄漏 + **信号重复连接**（`sampleReady` 一条样本进两次 `onSampleReady`，计数翻倍）+ `addDatabase` 连接名冲突（连接名用 `this` 指针地址拼接，`SqliteRepository.cpp:11`，老对象 delete 后新对象极可能分到同地址 → "duplicate connection name" 警告/覆盖）。
**为什么致命**：S9 的 `stop(); configure(); start();` 全量重启方案**在现在的代码上跑第二次必炸**。这不是理论问题，是 S9 的直接前置。
**修法**（1 小时）：`start()` 幂等化（`if (m_workThread) return;` 或先 stop）；`stop()` 补 `m_healthMonitor->deleteLater()`、`m_workThread->deleteLater()`、全部指针置空；`SqliteRepository` 加析构 `close + removeDatabase`（这也是 S1-2 原计划）。**注意**：修完后 "stop→start 反复 10 次" 是一个绝佳的自测用例。

### B5 历史表格每秒全量重建 350 个 QTableWidgetItem
**证据**：`pages/homepage.cpp:213-241` —— 每条 `historyUpdated`（每秒一次）执行 `setRowCount(50)` + 50 行×7 列 `new QTableWidgetItem`。QTableWidget 会接管并 delete 旧 item，所以**不是内存泄漏**，但每秒 350 次分配/释放 + 全表重绘，与"72 小时连续运行"的产品叙事直接矛盾。S4 做 10Hz 后变成**每秒 3500 次分配**。
**为什么致命**：这是 Qt 上位机面试的经典考点（Widget 类 vs Model/View）。你用了 QTableWidget 又用了全量重建，两头都被打：要么解释为什么不用 QTableView+QAbstractTableModel（更专业），要么改成增量 append。
**修法**（1 小时，两档）：① 最小改：`onHistoryUpdated` 只处理**最后一行**（insertRow + setItem 7 个），满 50 行时 `removeRow(0)`——每秒 7 个 item；② 进阶：QAbstractTableModel + data() 里按角色返回颜色（面试价值更高，2~3 小时）。当前建议①，S6 时若有余力再②。

### B6 pendingRecord 凑齐机制的跨轮污染
**证据**：`ServiceFacade.cpp:126-143` —— `size() >= 8` 即推送。若某指标一轮丢失（读失败不发该 name 的样本，或 bad 样本覆盖前一轮的值），**上一轮的旧值残留在 map 里凑数**，时间戳却是新一轮的——表格里出现"新旧混合"的一行。极端情况：`items` 只配 5 个指标时 size 永远 ≤7，**表格永久停止刷新**且不会报任何错。
**修法**（0.5~1 小时）：凑齐判定从"数 key 个数"改为"按 config 的 items 名单逐项 `contains` 检查 + 超时强制推送"（QElapsedTimer，比如 1.5s 攒不齐就推，缺项填 NaN/空）。**面试话术**："帧聚合不能靠数格子，要按模式（schema）核对 + 超时兜底，否则慢/丢采样会把两轮数据缝成一行"。

### B7 TestRunner 三个语义缺陷
**证据**：`src/test/TestRunner.cpp`
- `recordSample`（:53-57）不过滤 quality——现在没出事是因为 `decode` bad 时 `value=0`、`qMax` 取大天然免疫，**是巧合安全不是设计安全**（哪天驱动 decode 失败返回垃圾值，峰值被污染 → 误判 FAIL）；
- `stop()`（:26-35）中途停止也走 `evaluateVerdict()` 判 pass/fail——测试没跑完应该判 "aborted"，中途停止判 PASS 是工业软件大忌；
- `remainingSeconds = testDurationHours * 3600`（:45）——小时粒度，**演示 72h 测试只能等 72 小时**，改配置演示要么等不起要么把 72 改成 0.01 这种丑数字。
**修法**（1 小时）：recordSample 加 `quality=="good"` 门；stop 区分"到时结束"与"人为中止"（verdict 增 "aborted"，UI 显示灰）；duration 支持 `testDurationMinutes` 字段（config 向后兼容）——**演示配置写 2 分钟，PASS/FAIL 90 秒内可见，这条直接决定演示脚本能不能走完**。

### B8 阈值下发无输入校验，一次手滑引发告警风暴
**证据**：`settingspage.cpp:84-89` 空输入 `toDouble()=0`；`ServiceFacade.cpp:163` `static_cast<quint16>(values[i] / regs[i].scale)`——scale=0 除零、负数 cast UB、空输入把温度阈值写成 0（`isUpper` 规则 `value>=0` 恒真 → 6 条规则每秒全触发，日志/标红风暴）。
**修法**（0.5 小时）：Save 前 `QDoubleValidator` 或手动 `ok` 检查 + qBound 钳位 + scale<=0 防御。

### B9 configure() 公开接口无跨线程防护
**证据**：`ServiceFacade::configure`（`ServiceFacade.cpp:39`）直接调 `m_pollingScheduler->rebuildTasks(config)`。首次在 `start()` 内调用时对象尚未 moveToThread（`:68` 在 `:70` 之前）所以碰巧安全；但 configure 是 **public** 接口，start 之后任何人从主线程调它 = **跨线程直接方法调用**，绕过你在 `onConfigUpdated` 里正确使用的 invokeMethod 模式。
**修法**（10 分钟）：configure 内部的 scheduler 调用统一走 `invokeMethod`，或 configure 收编进 onConfigUpdated 同一路径。**话术**："moveToThread 之前调用碰巧安全"这种脆弱性要能自己指出来——比被面试官指出来强十倍。

### B10 "InputRegister 假支持"——三个层次各有一段死代码，拼起来却是一条断头链
**证据链**：`ModbusTypes.h:7-12` 定义了 4 种 `RegisterType`；`config.cpp:93` 解析了 `registerType` 字段；`ModbusTypes.cpp` 有 `registerTypeFromString`；模拟器实现了完整的 0x04 读输入寄存器响应（`modbus_simulator.cpp:194-204`）。**但**：驱动全部写死 `RegisterType::HoldingRegister`（`motortemperaturedevice.h:21`、`simpleregisterdevice.h:21`）；`ModbusTcpClient::readRegisters` 只有 `modbus_read_registers`（03 功能码，`modbustcpclient.cpp:43`），**没有 `modbus_read_input_registers`**；`registerTypeFromString` 全工程零调用；`registerTypeToString` 同。
**为什么致命**：面试官问"支持输入寄存器吗（04 功能码）？"——你有枚举、有配置字段、有模拟器实现，自然答"支持"；他翻 `readRegisters` 只有一个 03。**"看似支持实则断头"比坦诚"只做了 03"更伤可信度**。
**修法**（1 小时）：`readRegisters` 按 `req.type` 分发 `modbus_read_registers` / `modbus_read_input_registers` / `modbus_read_input_bits` / `modbus_read_bits`（S7 原计划 26 节本就要求）；`PollItem.registerType` 传入驱动。或者——**删掉枚举里的死分支和死函数**，明说"当前只支持保持寄存器，接口留了扩展位"。二选一都比现状强。

### B11 其余小项（30 分钟内可清完）
- `Logger::emitLine`（`logger.cpp:82`）在锁外读 `m_level`（`setLevel` 有锁）——TSAN 级竞争，把判断挪进锁内即修；这是 `ca96461` 竞争修复的漏网之鱼，面试讲那次修复时**主动提**"复查还发现 Logger 级别读没进锁"——体现修复的系统性；
- `ModbusSession::connectionChanged` 信号 3 处 emit、0 处 connect（死信号）——正好是 S5 `modbusConnected` 属性的现成数据源，接线或删除；
- `refreshUiState`（`ServiceFacade.cpp:224-225`）`healthyChanged` 每秒无条件 emit，而上面 4 行的 `modbusConnectedChanged` 做了边沿检测——同一函数两种风格， NOTIFY 惯例是只在变化时发；
- `DataCache` / `MetricsCollector` 全工程只写不读（唯一消费者 `DiagnosticsReporter` 未实例化）——每样本两次锁开销买了个寂寞，接线（产出性能数字）或删除；
- 模拟器 `WriteSingleRegister` 分支把写值复用在变量名 `quantity` 里（`modbus_simulator.cpp:206`）——功能对但语义误导，改名 `value`。

---

## C 类：深挖架构 10 分钟会遇到的话题（讲得好是加分项）

### C1 `onSampleReady` 是一个"上帝槽"：一条样本驱动 8 件事
**证据**：`ServiceFacade.cpp:116-152`——UI 卡片、pendingRecord 聚合、规则评估、表格推送、TestRunner 峰值、DataCache、Metrics、SQLite 落库、计数，全部串在一个槽里。
**两面性**：往好了讲，这是"一条数据总线"（门面统一分发，顺序明确）；往坏了讲，任何一个消费者变慢都拖住其他 7 个（都在主线程）。S5/S6 拆属性体系时顺手把它拆成"分发 + 各自槽"是自然演进。**不必专门重构**，但被问到要有准备：知道这是单线程事件循环的分发点，知道瓶颈在 SQLite（已用 invokeMethod 挪走）和未来 10Hz 的表格刷新。

### C2 调度器的串行 tick 模型 + libmodbus 阻塞语义
**证据**：`PollingScheduler::tick`（`PollingScheduler.cpp:58-76`）——单线程 for 循环，到点任务串行 `m_session->send()`（阻塞 0.5s 超时）。6 任务同 tick 到期（`lastPollMs=0` 初始，启动第一拍全部触发，`:84`）时最坏 3 秒才能轮完一圈。
**这是 S7 的立论基础**，现在你能给出完整的故障量化：断线时每请求 0.5s × 6 任务/tick × tick 100ms——调度循环完全被 IO 吃掉。**S7 做完前后各测一组"断线恢复时间"数字**，就是简历上异步化那条的定量证据。

### C3 写路径三连 invokeMethod 的重复模式
**证据**：`ServiceFacade.cpp:166-168 / 199-201 / 211-213` 三段几乎相同的"构造 req → invokeMethod → write"。S7 改回调时顺手提取 `writeAsync(req, callback)` 私有辅助函数，代码量和面试观感双收益。

### C4 模拟器 `buildWriteResponse` 里那条注释是个真故事
**证据**：`modbus_simulator.cpp:252-253`——"不能先 out<<6 字节再 rsp.append 再 out<<，QDataStream 内部位置会覆盖 append 的内容"。这是你实际踩过的坑（`buildReadResponse`/`buildException` 用"全部 QDataStream 或先流后 append 且不再回流"两种安全写法）。**面试讲 QDataStream 时这就是你的第一手素材**——比背 API 手册有说服力得多。注意三个封包函数风格不完全统一，被问到要能解释为什么 buildWriteResponse 必须全程用流。

---

## D. 对当前施工计划的修正建议

**核心变化：在 S5/S6 之前插入一个"演示链路修复阶段"（半天~1 天），并把 S7 的范围加两条硬性验收。**

### D1 S3 提交前必须带上（合计约 2 小时）
| 项 | 来源 | 工作量 |
|---|---|---|
| `config.json` 加 runStatus 采集项（地址 12），让启停闭环 UI 可见 | A3 | 0.5h |
| 调整 autoWalk/rateLimit 参数，让 current_rate 规则可触发演示 | A5 | 10min |
| bad 样本不刷实时卡片 | A4 | 0.5h |
| Settings 页 Test Profile 死 UI 处理（先删，S6 时再做真绑定） | A1 | 0.5h |

不带这四条，"-s 一键演示"的成色会打折：闭环不可见、变化率规则沉睡、断线闪 0、设置页穿帮。

### D2 S7 范围追加两条硬性验收（否则异步化不完整）
1. **`timeoutMs` 配置真正生效**：`modbus_set_response_timeout` + connect 路径不阻塞调度线程（B2）；
2. **读/写失败在回调里驱动重连**：断线检测从"HealthMonitor 10s 兜底"升级为"通信层即时感知"（B3），写结果回 UI（消除乐观更新的谎言）。

原有范围（队列 FIFO + QtConcurrent + 回对象线程回调 + QMutex 护 ctx + 异常码 isException/exceptionCode）不变。S7 完成后跑一次"拔网线"实验，记录断线→恢复全程时间线——这是简历异步条目的定量弹药。

### D3 优先级重排（面试暴露速度驱动）
| 优先 | 内容 | 工作量 | 对应 |
|---|---|---|---|
| **P0-α（本周，S3 收尾一起）** | D1 四项 + S3 验收提交 | ~2h + 0.5天 | A1/A3/A4/A5 |
| **P0-β（本周）** | S7 异步化（含 D2 两条验收） | 3~4天 | B2/B3 |
| **P0-γ（S7 后顺手）** | start/stop 幂等化 + SqliteRepository 析构 + configure 线程安全 | 1.5h | B4/B9/S1-2 |
| **P1-α** | 热更新配置校验 + 表格增量刷新 + pendingRecord 按模式凑齐 + TestRunner 三修 + 阈值输入校验 | 1天 | B1/B5/B6/B7/B8 |
| **P1-β** | S8 三线程 + S1 主键 + S2 降级 + exec 检查 | 2天 | 原 S1/S2/S8 |
| **P1-γ** | InputRegister 链路补全**或**死代码删除（二选一，别留断头链） | 1h | B10 |
| **P2** | S5/S6 属性体系（Test Profile 真绑定在此做）+ Logger 锁 + 死信号清理 + 曲线双轴 | 3~4天 | C1/A2/B11 |
| **P3** | 测试最小集 / CI / 截图 / LICENSE（v1 报告 H4/H5/S1 不变，仍然成立） | 2~3天 | v1 遗留 |

**与 v1 的差异说明**：v1 的工程化建议（测试/CI/物料）全部保留为 P3/P2，但让位给上面这些**源码里真实存在的缺陷**——面试官先看到代码和演示的问题，才会去看你有没有 CI。

---

## E. 面试防线速查（v2 新增的高频追问与应答要点）

| 追问 | 现状风险 | 应答策略 |
|---|---|---|
| "演示一下启停控制闭环" | 镜像无人消费，按钮只是自己变文字 | 修 A3 后：runStatus 卡片 0→1，讲"写 Coil→从站执行→遥测回读"完整环 |
| "演示变化率告警" | 规则永不触发 | 修 A5 后可演示；主动讲两帧差值的局限与窗口算法的演进计划 |
| "把 config 改坏会怎样？" | 采集静默死亡 | 修 B1 前别接这个话题；修后可讲"验证-拒绝-保持旧配置"原则 |
| "设备断电 vs 拔网线，行为一样吗？" | 不一样：EOF 快速失败 vs 0.5s×6 超时+21s connect 冻结 | 这是你能讲出的**最深的网络细节**：TCP 优雅关闭与静默丢包在 Modbus 层的不同表现，以及超时分层（响应超时/connect 超时/健康兜底）|
| "表格怎么更新的？" | 每秒 350 个 item 全量重建 | 修 B5 前别主动提；修后讲"增量 append + 上限裁剪"，或 Model/View 演进 |
| "测试没跑完就停了，判什么？" | 判 PASS（错误） | 修 B7 后答 "aborted，三态判定" |
| "输入寄存器支持吗？" | 假支持（枚举/配置/模拟器都有，client 只有 03） | 修 B10 前答"当前只实现了保持寄存器"——**诚实比被拆穿体面** |
| "你的线程竞争怎么修的？"（ca96461） | 修得对但 Logger m_level 漏网 | 主动讲 atomic 化 + "复查发现 Logger 级别读在锁外"——展示系统性 |

---

## F. 结语

v1 说"短板在工程化证据"，v2 的结论更进一步：**在补测试和 CI 之前，先把自己代码里那三条"看起来通了、实际断着"的链路修直**（启停镜像、rate 规则、InputRegister），再把故障场景的行为修到能拿数字讲（断线时间线、重连层级）。这些一共不到 4 天，却直接决定演示和追问两个战场的胜负。S7 依旧是最大单点，但带上 D2 的两条验收才算完整——异步化的意义不是"用了线程池"，是**故障被即时感知、超时被显式控制**。

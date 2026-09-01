# BRANCH_NOTES — `feat/simulator-async` 分支全记录

> 本文件记录该分支的**全部实质改动**：每个模块做了什么、为什么这么做、设计取舍、
> 踩过的 Bug 与排查过程、实测验证数据。写给人看——尤其写给 6 个月后的自己。
>
> 基线：`ca96461`（fix: 线程竞争/坏数据消费/路径依赖/anomaly标红）
> 分支终点状态：S1~S8 全部落地，S9 未做（见"遗留"）。

---

## 0. 提交史（为什么是三次提交）

| 提交 | 内容 | 说明 |
|---|---|---|
| `4452b94` | 模拟器 + 通信层异步化 | 作者本人提交（S3 + S7 主体） |
| `f77279f` | S1/S2 SQLite 加固与降级 + S4 调度侧 + 文档入库 | 含一条重要修正：**提交消息经 `--amend` 改写过**（见 Bug 4） |
| `ea04cd0` | S4 门面侧 + S5 属性体系 + S6 UI 绑定 + S8 存储线程 | 本次会话主产出 |
| （待提交） | P0 演示链路修复（启停闭环 / rate 可触发 / 坏数据防闪 / Test Profile 接线 / timeoutMs 透传） | 见第 8 节 |

分三次的原因：**一次提交只讲一件事，diff 可独立审查、可单独回滚**。S1/S2 是用户先写的，单独入库不与异步化混在一起。

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

---

## 11. 遗留事项（未做，需决策）

1. **S9 热更新全量重启**：`stop → configure → start` + start() 幂等化——`configure()` 已线程安全，只剩全量重启这层壳
2. 热更新配置校验（ConfigWatcher 加载失败应拒绝新配置、保持旧配置）
3. 表格增量刷新（当前每秒全量重建 350 个 QTableWidgetItem）
4. `pendingRecord` 按 items 名单核对 + 超时兜底（当前按 key 个数凑齐，丢采样会串帧）
5. TestRunner 三修：bad 不入峰值 / 中途停止判 aborted / duration 支持分钟级
6. 曲线双 Y 轴（当前单温度曲线、Y 轴 20~100 硬编码）
7. InputRegister 链路补全或删死分支（枚举/配置/模拟器有 04，client 只有 03）
8. 阈值写"成功才回写快照"（当前写前即更，回调在工作线程有跨线程问题）
9. `endpoint.timeoutMs` 已透传，但 connect 超时与响应超时的分层验收实验未做（拔网线时间线）
10. 规则触发自动停机联动
11. `a.h/a.cpp` 异步草稿的去留（已排除出构建，占磁盘）

---

*维护约定：代码行为变化后，同步更新本文档与 README；所有数字必须来自实测，不引用文档自述。*

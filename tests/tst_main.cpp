// 单元测试最小集：只测纯逻辑组件（规则引擎 / TestRunner / 配置解析），
// 不碰 GUI、不碰线程 —— 这些组件无 Qt 事件循环依赖，QTest 可离线跑。
// 运行：构建后 ctest 或直接执行 unit_tests.exe
#include <QtTest>
#include <QJsonObject>
#include <QJsonArray>

#include "rules/RuleEngine.h"
#include "rules/thresholdrule.h"
#include "rules/ratechangerule.h"
#include "test/TestRunner.h"
#include "config/config.h"

class TstCore : public QObject
{
    Q_OBJECT
private slots:
    void thresholdTrigger();
    void rateTrigger();
    void engineCount();
    void runnerDurations();
    void runnerBadSample();
    void runnerAbort();
    void configParse();
};

// ── 阈值规则：上下限判定 ──
void TstCore::thresholdTrigger()
{
    ThresholdRule r("temp_high", "temperature", 85.0, true);
    TelemetrySample s;
    s.name = "temperature";
    s.value = 90.0;
    QVERIFY(r.evaluate(s).triggered);     // 90 >= 85 → 触发
    s.value = 80.0;
    QVERIFY(!r.evaluate(s).triggered);    // 80 < 85 → 不触发

    ThresholdRule low("rpm_low", "rpm", 300.0, false);
    s.name = "rpm";
    s.value = 250.0;
    QVERIFY(low.evaluate(s).triggered);   // isUpper=false：低于阈值触发
    s.value = 400.0;
    QVERIFY(!low.evaluate(s).triggered);
}

// ── 变化率规则：两帧差值 / Δt ──
void TstCore::rateTrigger()
{
    RateChangeRule r("current_rate", "current", 2.5);
    TelemetrySample a, b;
    a.name = "current"; a.value = 10.0; a.timestampMs = 0;
    b.name = "current"; b.value = 14.0; b.timestampMs = 1000;
    QVERIFY(!r.evaluate(a).triggered);    // 首帧只记录基线
    QVERIFY(r.evaluate(b).triggered);     // (14-10)/1s = 4 >= 2.5 → 触发
    b.value = 11.0;
    QVERIFY(!r.evaluate(b).triggered);    // 1 A/s < 2.5 → 不触发
}

// ── 规则引擎：增删计数 + 触发信号 ──
void TstCore::engineCount()
{
    RuleEngine engine;
    QCOMPARE(engine.ruleCount(), 0);
    engine.addRule(std::make_unique<ThresholdRule>("t1", "temperature", 85.0, true));
    engine.addRule(std::make_unique<ThresholdRule>("t2", "current", 20.0, true));
    QCOMPARE(engine.ruleCount(), 2);

    QSignalSpy spy(&engine, &RuleEngine::ruleTriggered);
    TelemetrySample s;
    s.name = "temperature"; s.value = 90.0; s.quality = "good";
    engine.evaluate(s);
    QCOMPARE(spy.count(), 1);             // 只有 temp_high 命中
    engine.clear();
    QCOMPARE(engine.ruleCount(), 0);
}

// ── TestRunner：小时 / 分钟级时长 ──
void TstCore::runnerDurations()
{
    TestProfile p;
    p.testDurationHours = 72;
    TestRunner tr;
    tr.setProfile(p);
    QCOMPARE(tr.remainingSeconds(), 72 * 3600);

    TestProfile pm;
    pm.testDurationMinutes = 2;
    tr.setProfile(pm);
    QCOMPARE(tr.remainingSeconds(), 120);
}

// ── TestRunner：bad 样本不进峰值（解码失败值 0 会污染 qMax）──
void TstCore::runnerBadSample()
{
    TestProfile p;
    p.maxMotorTemp = 85.0;
    p.maxVibration = 4.5;
    TestRunner tr;
    tr.setProfile(p);
    tr.start();                              // 重置峰值
    tr.recordSample("temperature", 100.0, false);   // bad：100 被忽略
    tr.recordSample("temperature", 80.0, true);     // good：80 进峰值
    tr.recordSample("vibration", 3.0, true);
    tr.stop();                               // 自然结束 → evaluate
    QCOMPARE(tr.verdict(), "pass");          // 若 bad 100 进了峰值会判 fail
}

// ── TestRunner：人为中止判 aborted（不算 pass/fail）──
void TstCore::runnerAbort()
{
    TestProfile p;
    p.maxMotorTemp = 85.0;
    TestRunner tr;
    tr.setProfile(p);
    tr.start();
    tr.recordSample("temperature", 100.0, true);   // 若正常结束会判 fail
    tr.abort();
    QCOMPARE(tr.verdict(), "aborted");
}

// ── ConfigLoader：端点 timeout / items registerType / 规则 autoStop 解析 ──
void TstCore::configParse()
{
    QJsonArray items;
    items.append(QJsonObject{{"name", "temperature"}, {"address", 5}});
    items.append(QJsonObject{{"name", "runStatus"}, {"address", 12}, {"registerType", "input"}});
    QJsonArray rules;
    rules.append(QJsonObject{{"name", "temp_high"}, {"type", "threshold"},
                             {"metric", "temperature"}, {"threshold", 85.0}, {"autoStop", true}});
    QJsonObject obj;
    obj.insert("endpoint", QJsonObject{{"host", "127.0.0.1"}, {"port", 502},
                                       {"unitId", 1}, {"timeoutMs", 1500}});
    obj.insert("items", items);
    obj.insert("rules", rules);

    Config c = ConfigLoader().loadFromJson(obj);
    QCOMPARE(c.endpoint.host, QString("127.0.0.1"));
    QCOMPARE(c.endpoint.timeoutMs, 1500);          // 解析贯通
    QCOMPARE(c.items.size(), 2);
    QCOMPARE(c.items[1].registerType, QString("input"));  // B10：registerType 解析
    QCOMPARE(c.rules.size(), 1);
    QVERIFY(c.rules[0].autoStop);                  // 自动停机标志解析
}

QTEST_GUILESS_MAIN(TstCore)
#include "tst_main.moc"

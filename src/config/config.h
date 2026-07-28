#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace burninsys {

// ── Modbus 连接端点 ──
struct ModbusEndpoint
{
    QString host{"127.0.0.1"};
    int port{502};           // Modbus TCP 标准端口
    int unitId{1};           // 从站地址（PLC编号）
    int timeoutMs{2000};     // 读写超时
};

// ── 采集项：一个指标对应一个寄存器 ──
struct PollItem
{
    QString name;            // 指标名，如 "motorTemperature"
    QString registerType;    // "holding" 或 "input"
    quint16 address{0};          // Modbus 寄存器地址
    quint16 length{1};           // 读几个寄存器（32位浮点需要2个）
    int intervalMs{1000};    // 采样间隔
    double scale{1.0};       // 缩放系数：原始值 × scale = 实际值
};

// ── 规则配置 ──
struct RuleConfig
{
    QString name;            // 规则名，如 "temperature_high"
    QString type;            // "threshold" 或 "rate"
    double threshold{0.0};   // 阈值
    double windowSeconds{10.0}; // 滑动窗口（变化率规则用）
    double rateLimit{0.0};   // 变化率上限（变化率规则用）
};

// ── 老化测试规程 ──
struct TestProfile
{
    int testDurationHours{72};       // 测试时长（72小时=3天标准老化）
    int ratedRpm{1500};              // 额定转速
    double ratedLoad{1.0};           // 额定负载系数（1.0=满载）
    double maxMotorTemp{85.0};       // 合格阈值：最高温度
    double maxVibration{4.5};        // 合格阈值：最大振动（ISO 10816标准）
    int sampleIntervalMs{1000};      // 正常采样间隔
    int anomalySampleIntervalMs{100}; // 异常采样间隔（10Hz高频）
};

// ── 聚合配置 ──
struct Config
{
    ModbusEndpoint endpoint;
    QVector<PollItem> items;
    QVector<RuleConfig> rules;
    TestProfile testProfile;
    QString dataFilePath{"data/telemetry.db"};
};

// ── 配置加载器 ──
class ConfigLoader
{
public:
    Config loadFromFile(const QString &path);
    Config loadFromJson(const QJsonObject &obj);

private:
    ModbusEndpoint parseEndpoint(const QJsonObject &obj) const;
    QVector<PollItem> parseItems(const QJsonArray &arr) const;
    QVector<RuleConfig> parseRules(const QJsonArray &arr) const;
};

} // namespace burninsys

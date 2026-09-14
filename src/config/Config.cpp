#include "Config.h"
#include "logging/Logger.h"
#include <QFile>
#include <QJsonDocument>

using burninsys::Logger;

Config ConfigLoader::loadFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        Logger::instance().warn(QStringLiteral("Failed to open config: %1").arg(path));
        return {};  // 返回默认 Config
    }
    const auto data = file.readAll();
    const auto doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
    {
        Logger::instance().warn(QStringLiteral("Invalid JSON config: %1").arg(path));
        return {};
    }
    return loadFromJson(doc.object());
}

Config ConfigLoader::loadFromJson(const QJsonObject &obj)
{
    Config cfg;
    cfg.simulate = obj.value("simulate").toBool(cfg.simulate);
    if (obj.contains("endpoint"))
        cfg.endpoint = parseEndpoint(obj.value("endpoint").toObject());
    if (obj.contains("motorCommand"))
        cfg.motorCommand = parseMotorCommand(obj.value("motorCommand").toObject());
    if (obj.contains("modeCommand"))
        cfg.modeCommand = parseModeCommand(obj.value("modeCommand").toObject());
    if (obj.contains("testProfile"))
        cfg.testProfile = parseTestProfile(obj.value("testProfile").toObject());
    if (obj.contains("items"))
        cfg.items = parseItems(obj.value("items").toArray());
    if (obj.contains("rules"))
        cfg.rules = parseRules(obj.value("rules").toArray());
    if (obj.contains("thresholdRegisters"))
        cfg.thresholdRegisters = parseThresholdRegisters(obj.value("thresholdRegisters").toArray());
    cfg.dataFilePath = obj.value("dataFilePath").toString(cfg.dataFilePath);
    return cfg;
}

ModbusEndpoint ConfigLoader::parseEndpoint(const QJsonObject &obj) const
{
    ModbusEndpoint ep;
    ep.host      = obj.value("host").toString(ep.host);
    ep.port      = obj.value("port").toInt(ep.port);
    ep.unitId    = obj.value("unitId").toInt(ep.unitId);
    ep.timeoutMs = obj.value("timeoutMs").toInt(ep.timeoutMs);
    return ep;
}

MotorCommand ConfigLoader::parseMotorCommand(const QJsonObject &obj) const
{
    MotorCommand mc;
    mc.address = obj.value("address").toInt(mc.address);
    mc.startValue = obj.value("startValue").toInt(mc.startValue);
    mc.stopValue = obj.value("stopValue").toInt(mc.stopValue);
    return mc;
}

ModeCommand ConfigLoader::parseModeCommand(const QJsonObject &obj) const
{
    ModeCommand mc;
    mc.address = obj.value("address").toInt(mc.address);
    mc.manual = obj.value("manual").toInt(mc.manual);
    mc.autoMode = obj.value("auto").toInt(mc.autoMode);
    mc.test = obj.value("test").toInt(mc.test);
    return mc;
}

TestProfile ConfigLoader::parseTestProfile(const QJsonObject &obj) const
{
    TestProfile tp;
    tp.testDurationHours = obj.value("testDurationHours").toInt(tp.testDurationHours);
    tp.testDurationMinutes = obj.value("testDurationMinutes").toInt(tp.testDurationMinutes);
    tp.maxMotorTemp = obj.value("maxMotorTemp").toDouble(tp.maxMotorTemp);
    tp.maxVibration = obj.value("maxVibration").toDouble(tp.maxVibration);
    tp.sampleIntervalMs = obj.value("sampleIntervalMs").toInt(tp.sampleIntervalMs);
    // 异常高频采样的目标间隔（默认 100ms = 10Hz）。config.json 里不写就用结构体默认值。
    tp.anomalySampleIntervalMs = obj.value("anomalySampleIntervalMs").toInt(tp.anomalySampleIntervalMs);
    return tp;
}

QVector<PollItem> ConfigLoader::parseItems(const QJsonArray &arr) const
{
    QVector<PollItem> items;
    for (const auto &value : arr)
    {
        const auto obj = value.toObject();
        PollItem item;
        item.name         = obj.value("name").toString();
        item.registerType = obj.value("registerType").toString("holding");
        item.address      = obj.value("address").toInt(0);
        item.length       = obj.value("length").toInt(1);
        item.intervalMs   = obj.value("intervalMs").toInt(1000);
        item.scale        = obj.value("scale").toDouble(1.0);
        items.push_back(item);
    }
    return items;
}

QVector<RuleConfig> ConfigLoader::parseRules(const QJsonArray &arr) const
{
    QVector<RuleConfig> rules;
    for (const auto &value : arr)
    {
        const auto obj = value.toObject();
        RuleConfig rc;
        rc.name          = obj.value("name").toString();
        rc.type          = obj.value("type").toString("threshold");
        rc.metric        = obj.value("metric").toString();
        rc.threshold     = obj.value("threshold").toDouble(0.0);
        rc.windowSeconds = obj.value("windowSeconds").toDouble(10.0);
        rc.rateLimit     = obj.value("rateLimit").toDouble(0.0);
        rc.isUpper       = obj.value("isUpper").toBool(true);
        rc.autoStop      = obj.value("autoStop").toBool(false);
        rules.push_back(rc);
    }
    return rules;
}

QVector<ThresholdRegisterConfig> ConfigLoader::parseThresholdRegisters(const QJsonArray &arr) const
{
    QVector<ThresholdRegisterConfig> thresholdRegisters;
    for (const auto &value : arr)
    {
        const auto obj = value.toObject();
        ThresholdRegisterConfig rc;
        rc.name          = obj.value("name").toString();
        rc.address       = obj.value("address").toInt();
        rc.scale = obj.value("scale").toDouble(1.0);
        thresholdRegisters.push_back(rc);
    }
    return thresholdRegisters;
}


#include "config.h"

#include "logging/logger.h"

#include <QFile>
#include <QJsonDocument>

namespace burninsys {

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
    if (obj.contains("endpoint"))
        cfg.endpoint = parseEndpoint(obj.value("endpoint").toObject());
    if (obj.contains("items"))
        cfg.items = parseItems(obj.value("items").toArray());
    if (obj.contains("rules"))
        cfg.rules = parseRules(obj.value("rules").toArray());
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
        rc.threshold     = obj.value("threshold").toDouble(0.0);
        rc.windowSeconds = obj.value("windowSeconds").toDouble(10.0);
        rc.rateLimit     = obj.value("rateLimit").toDouble(0.0);
        rules.push_back(rc);
    }
    return rules;
}

} // namespace burninsys

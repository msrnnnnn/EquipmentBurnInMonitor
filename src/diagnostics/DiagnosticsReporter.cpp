#include "DiagnosticsReporter.h"

#include <QJsonArray>

DiagnosticsReporter::DiagnosticsReporter(DataCache *cache,
                                         MetricsCollector *metrics,
                                         HealthMonitor *health,
                                         QObject *parent)
    : QObject(parent), m_cache(cache), m_metrics(metrics), m_health(health)
{
}

QJsonObject DiagnosticsReporter::buildSnapshot() const
{
    QJsonObject root;

    // 健康状态
    if (m_health) {
        root["healthy"] = m_health->isHealthy();
    }

    // 指标统计（min/max/avg/count）
    if (m_metrics) {
        QJsonArray arr;
        for (const auto &m : m_metrics->allMetrics()) {
            QJsonObject obj;
            obj["name"] = m.name;
            obj["min"] = m.min;
            obj["max"] = m.max;
            obj["avg"] = m.avg;
            obj["count"] = static_cast<double>(m.count);
            arr.append(obj);
        }
        root["metrics"] = arr;
    }

    // 最新值
    if (m_cache) {
        QJsonObject latest;
        for (const auto &s : m_cache->snapshot()) {
            latest[s.name] = s.value;
        }
        root["latest"] = latest;
    }

    return root;
}

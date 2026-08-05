#ifndef DIAGNOSTICSREPORTER_H
#define DIAGNOSTICSREPORTER_H

#include "healthmonitor.h"
#include "data/datacache.h"
#include "metrics/MetricsCollector.h"

#include <QObject>
#include <QJsonObject>

class DiagnosticsReporter : public QObject
{
    Q_OBJECT
public:
    explicit DiagnosticsReporter(DataCache *cache,
                                 MetricsCollector *metrics,
                                 HealthMonitor *health,
                                 QObject *parent = nullptr);

    QJsonObject buildSnapshot() const;

private:
    DataCache *m_cache;
    MetricsCollector *m_metrics;
    HealthMonitor *m_health;
};

#endif // DIAGNOSTICSREPORTER_H

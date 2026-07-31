#ifndef METRICSCOLLECTOR_H
#define METRICSCOLLECTOR_H

#include "device/DeviceDriver.h"

#include <QHash>
#include <QList>
#include <QReadWriteLock>

struct Metric
{
    QString name;
    double min{0.0};
    double max{0.0};
    double avg{0.0};
    qint64 count{0};
};

class MetricsCollector
{
public:
    void record(const TelemetrySample &sample);
    Metric metric(const QString &name) const;
    QList<Metric> allMetrics() const;
    void reset();

private:
    mutable QReadWriteLock m_lock;
    QHash<QString, Metric> m_metrics;
};

#endif // METRICSCOLLECTOR_H

#include "MetricsCollector.h"

void MetricsCollector::record(const TelemetrySample &sample)
{
    if (sample.quality != "good") return;
    QWriteLocker locker(&m_lock);
    auto &m = m_metrics[sample.name];

    if (m.count == 0) {
        m.name = sample.name;
        m.min = sample.value;
        m.max = sample.value;
        m.avg = sample.value;
        m.count = 1;
        return;
    }

    m.count += 1;
    m.min = qMin(m.min, sample.value);
    m.max = qMax(m.max, sample.value);
    m.avg = ((m.avg * (m.count - 1)) + sample.value) / static_cast<double>(m.count);
}

Metric MetricsCollector::metric(const QString &name) const
{
    QReadLocker locker(&m_lock);
    return m_metrics.value(name);
}

QList<Metric> MetricsCollector::allMetrics() const
{
    QReadLocker locker(&m_lock);
    return m_metrics.values();
}

void MetricsCollector::reset()
{
    QWriteLocker locker(&m_lock);
    m_metrics.clear();
}

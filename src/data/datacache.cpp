#include "datacache.h"
#include <QWriteLocker>
DataCache::DataCache(){}

void DataCache::put(const TelemetrySample& samples)
{
    QWriteLocker locker(&m_lock);
    m_samples[samples.name] = samples;
}

TelemetrySample DataCache::latest(const QString &name) const
{
    QReadLocker locker(&m_lock);
    return m_samples.value(name);
}

QVector<TelemetrySample> DataCache::snapshot() const
{
    QReadLocker locker(&m_lock);
    QVector<TelemetrySample> allSamples{};
    for(const TelemetrySample& sample : m_samples)
    {
        allSamples.append(sample);
    }
    return allSamples;
}

#ifndef DATACACHE_H
#define DATACACHE_H

#include <QHash>
#include <QVector>
#include <QReadWriteLock>
#include "device/DeviceDriver.h"
class DataCache
{
public:
    DataCache();
    void put(const TelemetrySample& samples);
    TelemetrySample latest(const QString& name) const;
    QVector<TelemetrySample> snapshot() const;
signals:

private:
    mutable QReadWriteLock m_lock;
    QHash<QString, TelemetrySample> m_samples;
};

#endif // DATACACHE_H

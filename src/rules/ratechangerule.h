#ifndef RATECHANGERULE_H
#define RATECHANGERULE_H

#include "Rule.h"

class RateChangeRule : public Rule
{
    Q_OBJECT
public:
    explicit RateChangeRule(const QString &name, const QString &metric, double maxRate);
    QString name() const;
    RuleResult evaluate(const TelemetrySample &sample) const;
private:
    QString m_name;
    QString m_metric;
    double m_maxRate{0};
    mutable double m_lastValue{0};
    mutable qint64 m_lastTimestamp{0};
};

#endif // RATECHANGERULE_H

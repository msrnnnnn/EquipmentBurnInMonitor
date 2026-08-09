#ifndef THRESHOLDRULE_H
#define THRESHOLDRULE_H

#include "Rule.h"

class ThresholdRule : public Rule
{
    Q_OBJECT
public:
    explicit ThresholdRule(const QString &name, const QString &metric, double threshold);
    QString name() const;
    RuleResult evaluate(const TelemetrySample &sample) const;
private:
    QString m_name;
    QString m_metric;
    double m_threshold{0};
};

#endif // THRESHOLDRULE_H

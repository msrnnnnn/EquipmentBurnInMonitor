#include "thresholdrule.h"

ThresholdRule::ThresholdRule(const QString &name, const QString &metric, double threshold)
    : m_name(name), m_metric(metric), m_threshold(threshold) {}

QString ThresholdRule::name() const
{
    return m_name;
}

RuleResult ThresholdRule::evaluate(const TelemetrySample &sample) const
{
    if(m_metric == sample.name)
    {
        if(sample.value >= m_threshold)
        {
            RuleResult rrt;
            rrt.triggered = true;
            rrt.ruleName = m_name;
            rrt.message = QStringLiteral("%1 %2 >= %3").arg(m_metric).arg(sample.value).arg(m_threshold);
            return rrt;
        }
    }
    return {};
}

#include "ratechangerule.h"
#include <qdatetime.h>

RateChangeRule::RateChangeRule(const QString &name, const QString &metric, double maxRate)
    : m_name(name), m_metric(metric), m_maxRate(maxRate){}

QString RateChangeRule::name() const
{
    return m_name;
}

RuleResult RateChangeRule::evaluate(const TelemetrySample &sample) const
{
    if(m_metric == sample.name)
    {
        if (m_lastTimestamp == 0) {
            // 第一次收到，没有上次值可以比较，先记录，下次再算
            m_lastTimestamp = sample.timestampMs;
            m_lastValue = sample.value;
            return {};
        }
        if (sample.timestampMs == m_lastTimestamp) {
            return {};  // 时间差为0，跳过
        }
        double rate = qAbs(m_lastValue - sample.value)/((sample.timestampMs - m_lastTimestamp) / 1000);
        m_lastTimestamp = sample.timestampMs;
        m_lastValue = sample.value;
        if(rate >= m_maxRate)
        {
            RuleResult rrt;
            rrt.triggered = true;
            rrt.ruleName = m_name;
            rrt.message = QStringLiteral("%1 %2 >= %3").arg(m_metric).arg(rate).arg(m_maxRate);
            return rrt;
        }
    }
    return {};
}

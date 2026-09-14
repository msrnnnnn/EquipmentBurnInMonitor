#include "RateChangeRule.h"
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
        if (!m_hasBaseline) {
            // 第一次收到，没有上次值可以比较，先记录，下次再算
            m_hasBaseline = true;
            m_lastTimestamp = sample.timestampMs;
            m_lastValue = sample.value;
            return {};
        }
        if (sample.timestampMs == m_lastTimestamp) {
            return {};  // 时间差为0，跳过
        }
        // 注意除以 1000.0 而不是 1000：整数除法在 Δt < 1 秒时会得到 0，导致 rate 除零为 inf
        double rate = qAbs(m_lastValue - sample.value)/((sample.timestampMs - m_lastTimestamp) / 1000.0);
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

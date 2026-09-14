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
    // "是否已有基线"不能靠 m_lastTimestamp==0 判断：首帧时间戳可能恰好是 0
    // （如测试/回放数据），会永远被当成首帧，rate 永远算不出来。用独立标志。
    mutable bool m_hasBaseline{false};
};

#endif // RATECHANGERULE_H

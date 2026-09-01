#ifndef RULEENGINE_H
#define RULEENGINE_H

#include "Rule.h"

#include <QObject>

class RuleEngine : public QObject
{
    Q_OBJECT
public:
    explicit RuleEngine(QObject *parent = nullptr);

    void addRule(std::unique_ptr<Rule> rule);
    void evaluate(const TelemetrySample &sample);
    void clear();
    // S5：当前挂载的规则数，供 ServiceFacade 的 engineRuleCount 属性使用
    int ruleCount() const { return static_cast<int>(m_rules.size()); }
signals:
    void ruleTriggered(const RuleResult &result);

private:
    std::vector<std::unique_ptr<Rule>> m_rules;
};

#endif // RULEENGINE_H

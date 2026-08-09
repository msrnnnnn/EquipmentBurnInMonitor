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
signals:
    void ruleTriggered(const RuleResult &result);

private:
    std::vector<std::unique_ptr<Rule>> m_rules;
};

#endif // RULEENGINE_H

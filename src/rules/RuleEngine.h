#ifndef RULEENGINE_H
#define RULEENGINE_H

#include "Rule.h"

#include <QObject>
#include <QList>

class RuleEngine : public QObject
{
    Q_OBJECT
public:
    explicit RuleEngine(QObject *parent = nullptr);

    void addRule(Rule *rule);
    void evaluate(const TelemetrySample &sample);

signals:
    void ruleTriggered(const RuleResult &result);

private:
    QList<Rule*> m_rules;
};

#endif // RULEENGINE_H

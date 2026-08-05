#include "RuleEngine.h"
#include "logging/logger.h"

RuleEngine::RuleEngine(QObject *parent)
    : QObject{parent}
{}

void RuleEngine::addRule(Rule *rule)
{
    if (rule) {
        m_rules.append(rule);
    }
}

void RuleEngine::evaluate(const TelemetrySample &sample)
{
    for (auto *rule : m_rules) {
        RuleResult result = rule->evaluate(sample);
        if (result.triggered) {
            emit ruleTriggered(result);
        }
    }
}

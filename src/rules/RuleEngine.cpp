#include "RuleEngine.h"

RuleEngine::RuleEngine(QObject *parent)
    : QObject{parent}
{}

void RuleEngine::addRule(std::unique_ptr<Rule> rule)
{
    if (rule) {
        m_rules.emplace_back(std::move(rule));
    }
}

QList<RuleResult> RuleEngine::evaluate(const TelemetrySample &sample)
{
    QList<RuleResult> results;
    for (const auto& rule : m_rules) {
        RuleResult result = rule->evaluate(sample);
        if (result.triggered) {
            emit ruleTriggered(result);
            results.append(result);
        }
    }
    return results;
}

void RuleEngine::clear()
{
    m_rules.clear();
}

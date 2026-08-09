#include "RuleEngine.h"
#include "logging/logger.h"

RuleEngine::RuleEngine(QObject *parent)
    : QObject{parent}
{}

void RuleEngine::addRule(std::unique_ptr<Rule> rule)
{
    if (rule) {
        m_rules.emplace_back(std::move(rule));
    }
}

void RuleEngine::evaluate(const TelemetrySample &sample)
{
    for (const auto& rule : m_rules) {
        RuleResult result = rule->evaluate(sample);
        if (result.triggered) {
            emit ruleTriggered(result);
        }
    }
}

void RuleEngine::clear()
{
    m_rules.clear();
}

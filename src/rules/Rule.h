#ifndef RULE_H
#define RULE_H

#include "device/DeviceDriver.h"

#include <QObject>
#include <QString>

struct RuleResult
{
    bool triggered{false};
    QString ruleName;
    QString message;
};

class Rule : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual ~Rule() override = default;

    virtual QString name() const = 0;
    virtual RuleResult evaluate(const TelemetrySample &sample) const = 0;
};

#endif // RULE_H

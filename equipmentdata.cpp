#include "equipmentdata.h"

EquipmentData::EquipmentData(QObject *parent)
    : QObject{parent}
{}

// ── getter：直接返回成员变量 ──

double EquipmentData::getTemperature() const { return m_temperature; }
double EquipmentData::getCurrent() const     { return m_current; }
double EquipmentData::getRpm() const         { return m_rpm; }
double EquipmentData::getVibration() const   { return m_vibration; }
double EquipmentData::getVoltage() const     { return m_voltage; }
double EquipmentData::getPower() const       { return m_power; }

// ── setter：变化检查 → 赋值 → 发信号 ──

void EquipmentData::setTemperature(double value)
{
    if (qFuzzyCompare(m_temperature, value)) return;
    m_temperature = value;
    emit temperatureChanged();
}

void EquipmentData::setCurrent(double value)
{
    if (qFuzzyCompare(m_current, value)) return;
    m_current = value;
    emit currentChanged();
}

void EquipmentData::setRpm(double value)
{
    if (qFuzzyCompare(m_rpm, value)) return;
    m_rpm = value;
    emit rpmChanged();
}

void EquipmentData::setVibration(double value)
{
    if (qFuzzyCompare(m_vibration, value)) return;
    m_vibration = value;
    emit vibrationChanged();
}

void EquipmentData::setVoltage(double value)
{
    if (qFuzzyCompare(m_voltage, value)) return;
    m_voltage = value;
    emit voltageChanged();
}

void EquipmentData::setPower(double value)
{
    if (qFuzzyCompare(m_power, value)) return;
    m_power = value;
    emit powerChanged();
}

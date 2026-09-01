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
int EquipmentData::getRunStatus() const      { return m_runStatus; }

EquipmentDataProvider& EquipmentData::provider()
{
    return m_provider;
}

// ── setter：变化检查 → 赋值 → 发信号 ──
// [面试重点] 变化检查避免无意义刷新：工业上位机跑几天，不做过滤会疯狂刷UI
// qFuzzyCompare 不能直接比较含0.0的值（文档明确说不可靠），加1.0偏移规避

void EquipmentData::setTemperature(double value)
{
    if (qFuzzyCompare(1.0 + m_temperature, 1.0 + value)) return;
    m_temperature = value;
    emit temperatureChanged();
}

void EquipmentData::setCurrent(double value)
{
    if (qFuzzyCompare(1.0 + m_current, 1.0 + value)) return;
    m_current = value;
    emit currentChanged();
}

void EquipmentData::setRpm(double value)
{
    if (qFuzzyCompare(1.0 + m_rpm, 1.0 + value)) return;
    m_rpm = value;
    emit rpmChanged();
}

void EquipmentData::setVibration(double value)
{
    if (qFuzzyCompare(1.0 + m_vibration, 1.0 + value)) return;
    m_vibration = value;
    emit vibrationChanged();
}

void EquipmentData::setVoltage(double value)
{
    if (qFuzzyCompare(1.0 + m_voltage, 1.0 + value)) return;
    m_voltage = value;
    emit voltageChanged();
}

void EquipmentData::setPower(double value)
{
    if (qFuzzyCompare(1.0 + m_power, 1.0 + value)) return;
    m_power = value;
    emit powerChanged();
}

void EquipmentData::setRunStatus(int value)
{
    if (m_runStatus == value) return;   // 0/1 离散值，直接判等即可
    m_runStatus = value;
    emit runStatusChanged();
}

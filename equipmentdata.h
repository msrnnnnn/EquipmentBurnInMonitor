#ifndef EQUIPMENTDATA_H
#define EQUIPMENTDATA_H

#include <QObject>
#include "equipmentdataprovider.h"

class EquipmentData : public QObject
{
    Q_OBJECT
public:
    // ── 属性声明 ──
    // Q_PROPERTY：Qt属性系统，声明属性+读写方法+变化信号
    // UI层通过NOTIFY信号感知数据变化，实现观察者模式解耦
    Q_PROPERTY(double temperature READ getTemperature WRITE setTemperature NOTIFY temperatureChanged)
    Q_PROPERTY(double current READ getCurrent WRITE setCurrent NOTIFY currentChanged)
    Q_PROPERTY(double rpm READ getRpm WRITE setRpm NOTIFY rpmChanged)
    Q_PROPERTY(double vibration READ getVibration WRITE setVibration NOTIFY vibrationChanged)
    Q_PROPERTY(double voltage READ getVoltage WRITE setVoltage NOTIFY voltageChanged)
    Q_PROPERTY(double power READ getPower WRITE setPower NOTIFY powerChanged)
    // P0-1：设备运行状态（模拟器把启停线圈镜像到寄存器 12，归一化为 0/1）。
    // 它是"控制闭环"的最后一环：点启动 → 写线圈 → 从站执行 → 遥测回读 → UI 亮起。
    Q_PROPERTY(int runStatus READ getRunStatus WRITE setRunStatus NOTIFY runStatusChanged)

    explicit EquipmentData(QObject *parent = nullptr);

    // ── getter ──
    double getTemperature() const;
    double getCurrent() const;
    double getRpm() const;
    double getVibration() const;
    double getVoltage() const;
    double getPower() const;
    int getRunStatus() const;

    // 返回引用而非指针：控制访问方式，外部只能调用方法，不能delete或替换
    EquipmentDataProvider& provider();

    // ── setter ──
    void setTemperature(double value);
    void setCurrent(double value);
    void setRpm(double value);
    void setVibration(double value);
    void setVoltage(double value);
    void setPower(double value);
    void setRunStatus(int value);

signals:
    // 信号函数：只声明不实现，由moc自动生成。emit时触发所有connect的槽
    void temperatureChanged();
    void currentChanged();
    void rpmChanged();
    void vibrationChanged();
    void voltageChanged();
    void powerChanged();
    void runStatusChanged();

private:
    double m_temperature = 0.0;
    double m_current = 0.0;
    double m_rpm = 0.0;
    double m_vibration = 0.0;
    double m_voltage = 0.0;
    double m_power = 0.0;
    int m_runStatus = 0;

    // Data拥有Provider：组合关系，职责分离但生命周期绑定
    // Data管当前值，Provider管历史缓存+广播，各自单一职责
    EquipmentDataProvider m_provider;
};

#endif // EQUIPMENTDATA_H

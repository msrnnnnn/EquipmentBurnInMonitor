#ifndef EQUIPMENTDATA_H
#define EQUIPMENTDATA_H

#include <QObject>

class EquipmentData : public QObject
{
    Q_OBJECT
public:
    // ── 属性声明 ──
    Q_PROPERTY(double temperature READ getTemperature WRITE setTemperature NOTIFY temperatureChanged)
    Q_PROPERTY(double current READ getCurrent WRITE setCurrent NOTIFY currentChanged)
    Q_PROPERTY(double rpm READ getRpm WRITE setRpm NOTIFY rpmChanged)
    Q_PROPERTY(double vibration READ getVibration WRITE setVibration NOTIFY vibrationChanged)
    Q_PROPERTY(double voltage READ getVoltage WRITE setVoltage NOTIFY voltageChanged)
    Q_PROPERTY(double power READ getPower WRITE setPower NOTIFY powerChanged)

    explicit EquipmentData(QObject *parent = nullptr);

    // ── getter ──
    double getTemperature() const;
    double getCurrent() const;
    double getRpm() const;
    double getVibration() const;
    double getVoltage() const;
    double getPower() const;

    // ── setter ──
    void setTemperature(double value);
    void setCurrent(double value);
    void setRpm(double value);
    void setVibration(double value);
    void setVoltage(double value);
    void setPower(double value);

signals:
    void temperatureChanged();
    void currentChanged();
    void rpmChanged();
    void vibrationChanged();
    void voltageChanged();
    void powerChanged();

private:
    double m_temperature = 0.0;
    double m_current = 0.0;
    double m_rpm = 0.0;
    double m_vibration = 0.0;
    double m_voltage = 0.0;
    double m_power = 0.0;
};

#endif // EQUIPMENTDATA_H

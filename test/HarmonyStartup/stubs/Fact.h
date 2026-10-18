#pragma once
#include <QtCore/QObject>
#include <QtCore/QVariant>

class Fact : public QObject
{
    Q_OBJECT
public:
    explicit Fact(const QVariant& value) : _rawValue(value) {}

    QVariant rawValue() const { return _rawValue; }

    void setRawValue(const QVariant& value)
    {
        _rawValue = value;
        emit valueChanged(value);
    }

signals:
    void valueChanged(QVariant value);

private:
    QVariant _rawValue;
};

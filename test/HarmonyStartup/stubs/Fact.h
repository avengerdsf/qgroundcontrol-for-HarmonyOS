#pragma once

#include <QtCore/QObject>
#include <QtCore/QVariant>

class Fact : public QObject
{
    Q_OBJECT

public:
    explicit Fact(bool muted) : _rawValue(muted) {}
    QVariant rawValue() const { return _rawValue; }
    void setRawValue(bool muted)
    {
        _rawValue = muted;
        emit valueChanged(_rawValue);
    }

signals:
    void valueChanged(QVariant value);

private:
    QVariant _rawValue;
};

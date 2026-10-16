#pragma once
#include <QtCore/QStringList>
#include <QtCore/QVariant>

class Fact
{
public:
    QVariant rawValue() const { return value; }
    QVariant value = 63;
};

class ParameterManager
{
public:
    static constexpr int defaultComponentId = -1;
    bool parameterExists(int, const QString &) const { return hasMagSides; }
    Fact *getParameter(int, const QString &) { return &magSides; }
    void refreshParameter(int, const QString &name) { refreshed.append(name); }
    void refreshParametersPrefix(int, const QString &prefix) { refreshedPrefixes.append(prefix); }
    bool hasMagSides = false;
    Fact magSides;
    QStringList refreshed;
    QStringList refreshedPrefixes;
};

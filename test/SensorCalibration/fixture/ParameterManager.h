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

    bool parameterExists(int, const QString&) const { return hasMagSides; }

    Fact* getParameter(int, const QString&) { return &magSides; }

    void bulkRefresh(int, const QStringList& patterns, bool) { refreshed.append(patterns); }

    bool hasMagSides = false;
    Fact magSides;
    QStringList refreshed;
};

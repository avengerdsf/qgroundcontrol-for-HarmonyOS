#pragma once

#include <QtCore/QLoggingCategory>
#include <QtCore/QMap>
#include <QtCore/QObject>
#include <QtCore/QStringList>
#include <QtCore/QVariant>
#include <memory>

#include "QGCSerialPortInfo.h"

// Hardware enumeration and application services are the controlled boundaries.
// Board metadata, its production lookup/classification, and the entire warning
// method are compiled from current production source without rewriting bodies.
inline QList<HarmonySerialPortInfo> enumeratedPorts;

class LinkConfiguration : public QObject
{
    Q_OBJECT
public:
    enum LinkType
    {
        TypeSerial,
        TypeUdp
    };

    virtual LinkType type() const { return TypeUdp; }
};

class SerialConfiguration : public LinkConfiguration
{
    Q_OBJECT
public:
    QString name;

    LinkType type() const override { return TypeSerial; }

    QString portName() const { return name; }
};

class LinkInterface : public QObject
{
    Q_OBJECT
public:
    std::shared_ptr<LinkConfiguration> configuration;

    std::shared_ptr<LinkConfiguration> linkConfiguration() const { return configuration; }
};

using SharedLinkInterfacePtr = std::shared_ptr<LinkInterface>;

class SerialLink : public LinkInterface
{
    Q_OBJECT
public:
    const QSerialPort* port() const { return nullptr; }
};

class Fact
{
public:
    QVariant value;

    QVariant rawValue() const { return value; }
};

class ParameterManager
{
public:
    QMap<QString, Fact> parameters;

    bool parameterExists(int component, const QString& name) const
    {
        return component == -1 && parameters.contains(name);
    }

    Fact* getParameter(int component, const QString& name) { return component == -1 ? &parameters[name] : nullptr; }
};

class VehicleLinkManager
{
public:
    std::weak_ptr<LinkInterface> primary;

    std::weak_ptr<LinkInterface> primaryLink() const { return primary; }
};

class Vehicle
{
public:
    VehicleLinkManager links;
    ParameterManager parameters;

    VehicleLinkManager* vehicleLinkManager() { return &links; }

    ParameterManager* parameterManager() { return &parameters; }
};

class TestApplication
{
public:
    QStringList messages;

    void showAppMessage(const QString& message) { messages.append(message); }
};

inline TestApplication* qgcApp()
{
    static TestApplication application;
    return &application;
}

namespace QGC {
inline void showAppMessage(const QString& message, const QString& = QString())
{
    qgcApp()->showAppMessage(message);
}
}  // namespace QGC

class APMAutoPilotPlugin : public QObject
{
    Q_OBJECT
public:
    explicit APMAutoPilotPlugin(Vehicle* vehicle) : _vehicle(vehicle) {}

    void _checkForBadCubeBlack(bool parametersReady);

private:
    Vehicle* _vehicle;
};

#pragma once
#include <QtCore/QObject>
#include <memory>

#include "ParameterManager.h"
#include "QGCMAVLink.h"

class LinkConfiguration
{
public:
    enum LinkType
    {
        TypeSerial,
        TypeUdp
    };

    LinkType type() const { return TypeSerial; }
};

class LinkInterface
{
public:
    LinkConfiguration* linkConfiguration() { return &_configuration; }

private:
    LinkConfiguration _configuration;
};

using SharedLinkInterfacePtr = std::shared_ptr<LinkInterface>;

class VehicleLinkManager
{
public:
    std::weak_ptr<LinkInterface> primaryLink() const { return {}; }
};

class Vehicle : public QObject
{
    Q_OBJECT
public:
    int id() const { return 1; }

    int defaultComponentId() const { return 1; }

    VehicleLinkManager* vehicleLinkManager() { return &_links; }

    ParameterManager* parameterManager() { return &_parameters; }

    void startCalibration(QGCMAVLink::CalibrationType type) { starts.append(static_cast<int>(type)); }

    void stopCalibration(bool) { ++stops; }

    void sendMavCommand(int, MAV_CMD, bool, float, float) { ++otherCommands; }

    QList<int> starts;
    int stops = 0;
    int otherCommands = 0;
signals:
    void textMessageReceived(int uasId, int componentId, int severity, QString text, QString description);
    void sensorsParametersResetAck(bool success);

private:
    VehicleLinkManager _links;
    ParameterManager _parameters;
};

inline Vehicle* calibrationTestVehicle = nullptr;

#pragma once
#include "Vehicle.h"
#include <QtCore/QTimer>

class FactPanelController : public QObject
{
    Q_OBJECT
public:
    FactPanelController() : _vehicle(calibrationTestVehicle) {}
protected:
    Vehicle *_vehicle;
};

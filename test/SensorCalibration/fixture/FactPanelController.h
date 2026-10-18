#pragma once
#include <QtCore/QTimer>

#include "Vehicle.h"

class FactPanelController : public QObject
{
    Q_OBJECT
public:
    FactPanelController() : _vehicle(calibrationTestVehicle) {}

protected:
    Vehicle* _vehicle;
};

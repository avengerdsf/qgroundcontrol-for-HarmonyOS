#pragma once

#include "UnitTest.h"

class HarmonyNmeaTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _udpRmcGgaPosition();
    void _invalidChecksumDoesNotUpdate();
    void _stopAndRestartUpdates();
};

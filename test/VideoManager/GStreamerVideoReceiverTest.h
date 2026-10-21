#pragma once

#include "UnitTest.h"

class GStreamerVideoReceiverTest : public UnitTest
{
    Q_OBJECT
private slots:
    void initTestCase();
    void registryAndFactories();
    void sandboxEnvironment();
    void receiveAndRender_data();
    void receiveAndRender();
    void rtspTransport_data();
    void rtspTransport();
    void rtspsTrust_data();
    void rtspsTrust();
    void recordAndReplay();
    void stopRestartAndReconnect();
    void lowLatencyColorChange();
};

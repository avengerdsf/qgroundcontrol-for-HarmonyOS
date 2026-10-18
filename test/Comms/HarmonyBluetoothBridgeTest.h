#pragma once

#include "UnitTest.h"

class HarmonyBluetoothBridgeTest : public UnitTest
{
    Q_OBJECT
private slots:
    void cleanup();
    void _authorizationIsAsynchronous();
    void _discoveryUsesNativeAddresses();
    void _stopScanDiscardsLatePermission();
    void _closeDiscardsLateOpenAndRead();
    void _writesAreOrderedAndChunked();
    void _zeroWriteRetainsData();
    void _errorsCloseTheSession();
    void _destroyWithPendingOperations();
    void _capabilityDistinguishesFactoryFromHardware();
    void _configurationAndWorkerUseNativeBackend();
    void _linkThreadPublishesConnectionState();
    void _realProviderFactory();
};

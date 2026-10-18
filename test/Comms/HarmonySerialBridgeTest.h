#pragma once

#include "UnitTest.h"

class HarmonySerialBridgeTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _authorizationIsAsynchronous();
    void _officialBaudRatesAccepted_data();
    void _officialBaudRatesAccepted();
    void _lateOpenAndReadAreDiscarded();
    void _partialWritesAreOrderedAndChunked();
    void _zeroWriteRetainsQueuedData();
    void _closeCancelsZeroWriteRetry();
    void _unsupportedSettingsAreRejected();
    void _destroyDuringPendingOperations();
    void _reconnectDiscardsPreviousSession();
    void _pendingSerialLinkDisconnectCancelsLateOpen();
    void _fragmentedHeartbeatCreatesVehicle();
    void _realProviderEnumeration();
};

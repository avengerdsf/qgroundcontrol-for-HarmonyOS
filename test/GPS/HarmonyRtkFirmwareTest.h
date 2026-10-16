#pragma once

#include "UnitTest.h"

class HarmonyRtkFirmwareTest : public UnitTest
{
    Q_OBJECT
private slots:
    void _blockingTransportReadsFragmentedBytes();
    void _transportTimeoutAndCancellation();
    void _writeTimeoutClosesSession();
    void _baudTimeoutDiscardsLateCompletion();
    void _rtkTerminalReadIsError();
    void _gpsConfigureStopsOnCancelOrUnplug_data();
    void _gpsConfigureStopsOnCancelOrUnplug();
    void _rtkDisconnectFinishesBeforeDeletion();
    void _rtkCallbacksUseNativeTransport();
    void _bootloaderProgramsAndVerifies();
    void _bootloaderProgramsWithDelayedUsbCompletion();
    void _bootloaderRejectsCrcMismatch();
    void _bootloaderRejectsUnsupportedVersion();
    void _bootloaderTimesOutWithoutSync();
    void _bootloaderStopsOnUnplug();
    void _firmwareDisconnectDuringDownloadPreventsCommands();
    void _firmwareFailureNeverReportsSuccess_data();
    void _firmwareFailureNeverReportsSuccess();
    void _firmwareSuccessReportsOnce();
    void _firmwareCancelDuringErase();
    void _firmwareCancelAndRestart_data();
    void _firmwareCancelAndRestart();
    void _firmwareCancelStopsDiscovery();
    void _firmwareControllerRetainsSerialMetadata();
    void _firmwareControllerBestMatch_data();
    void _firmwareControllerBestMatch();
};

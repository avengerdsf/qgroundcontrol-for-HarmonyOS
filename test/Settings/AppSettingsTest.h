#pragma once

#include "UnitTest.h"

class AppSettingsTest : public UnitTest
{
    Q_OBJECT

protected slots:
    void init() final;
    void cleanup() final;

private slots:
    void _defaultSavePathWritable();
    void _legacyDefaultMigrated();
    void _customSavePathPreserved();
    void _telemetryMultiChunkArchived();
    void _failedTelemetryCopyRetained();
    void _invalidTelemetryPathRetained();
#if defined(Q_OS_HARMONY) && defined(QGC_ENABLE_BLUETOOTH)
    void _bluetoothLinkManagerRoundTrip_data();
    void _bluetoothLinkManagerRoundTrip();
#endif

private:
    QVariant _originalSavePath;
    QVariant _originalGlobalSavePath;
};

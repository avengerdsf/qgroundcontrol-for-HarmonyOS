#include <QtTest/QtTest>

#include "BluetoothConfiguration.h"

class NativeConfigurationTest : public QObject
{
    Q_OBJECT

private slots:
    void stopScanWithoutHarmonyBackend()
    {
        BluetoothConfiguration config(QStringLiteral("Native Bluetooth"));
        config.stopScan();
        QVERIFY(!config.scanning());
        config.setMode(BluetoothConfiguration::BluetoothMode::ModeLowEnergy);
        config.stopScan();
        QVERIFY(!config.scanning());
    }
};

QTEST_GUILESS_MAIN(NativeConfigurationTest)
#include "NativeConfigurationTest.moc"

#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "Fixture.h"

class FirmwareSerialMetadataTest : public QObject
{
    Q_OBJECT
private slots:

    void init()
    {
        HarmonySerialPortInfo board{42, QStringLiteral("USB device")};
        board.hasUsbIdentity = true;
        board.vendorId = 9900;
        board.productId = 17;
        board.manufacturer = QStringLiteral("Test manufacturer");
        board.productName = QStringLiteral("TestBoard-BL");
        enumeratedPorts = {board};
    }

    void stableUsbSerialIdentityIsRetained()
    {
        enumeratedPorts[0].serialNumber = QStringLiteral("board-A");
        QGCSerialPortInfo port(QStringLiteral("harmony-serial:42"));
        QCOMPARE(port.serialNumber(), QStringLiteral("board-A"));
        QCOMPARE(port.deviceName(), QStringLiteral("USB device"));
        enumeratedPorts[0].deviceName.clear();
        QVERIFY(QGCSerialPortInfo(QStringLiteral("harmony-serial:42")).deviceName().isEmpty());
        enumeratedPorts[0].hasUsbIdentity = false;
        QVERIFY(QGCSerialPortInfo(QStringLiteral("harmony-serial:42")).serialNumber().isEmpty());
    }

    void retainedProperties()
    {
        FirmwareUpgradeController controller;
        QSignalSpy found(&controller, &FirmwareUpgradeController::boardFound);
        controller._foundBoard(false, QGCSerialPortInfo(QStringLiteral("harmony-serial:42")),
                               QGCSerialPortInfo::BoardTypePixhawk, QStringLiteral("Test board"));
        QCOMPARE(found.count(), 1);
        QCOMPARE(controller.property("boardPort").toString(), QStringLiteral("harmony-serial:42"));
        QCOMPARE(controller.property("boardDescription").toString(), QStringLiteral("TestBoard-BL"));
    }

    void bestFirmwareMatch_data()
    {
        QTest::addColumn<QString>("description");
        QTest::addColumn<int>("best");
        QTest::newRow("matching-bootloader") << QStringLiteral("TestBoard-BL") << 1;
        QTest::newRow("other-bootloader") << QStringLiteral("Other-BL") << 0;
        QTest::newRow("application-port") << QStringLiteral("TestBoard") << 0;
    }

    void bestFirmwareMatch()
    {
        QFETCH(QString, description);
        QFETCH(int, best);
        enumeratedPorts[0].productName = description;
        FirmwareUpgradeController controller;
        for (int index = 0; index < 2; ++index) {
            FirmwareUpgradeController::ManifestFirmwareInfo_t entry{};
            entry.boardId = 42;
            entry.firmwareBuildType = FirmwareUpgradeController::StableFirmware;
            entry.vehicleType = FirmwareUpgradeController::CopterFirmware;
            entry.chibios = true;
            entry.friendlyName = QStringLiteral("Board %1").arg(index);
            entry.url = QStringLiteral("https://fixture.invalid/%1.apj").arg(index);
            entry.rgBootloaderPortString = {index ? QStringLiteral("TestBoard-BL") : QStringLiteral("Decoy-BL")};
            controller._rgManifestFirmwareInfo.append(entry);
        }
        controller._foundBoard(false, QGCSerialPortInfo(QStringLiteral("harmony-serial:42")),
                               QGCSerialPortInfo::BoardTypePixhawk, QStringLiteral("Test board"));
        QSignalSpy changed(&controller, &FirmwareUpgradeController::apmFirmwareNamesChanged);
        controller._buildAPMFirmwareNames();
        QCOMPARE(changed.count(), 1);
        QCOMPARE(controller.property("apmFirmwareNamesBestIndex").toInt(), best);
        const auto urls = controller.property("apmFirmwareUrls").toStringList();
        QCOMPARE(urls.size(), best ? 2 : 3);
        QCOMPARE(urls[best], best ? QStringLiteral("https://fixture.invalid/1.apj") : QString{});
    }
};
QTEST_GUILESS_MAIN(FirmwareSerialMetadataTest)
#include "FirmwareSerialMetadataTest.moc"

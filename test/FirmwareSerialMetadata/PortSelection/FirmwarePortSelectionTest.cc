#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "Fixture.h"

class FirmwarePortSelectionTest : public QObject
{
    Q_OBJECT
    const QGCSerialPortInfo a{QStringLiteral("harmony-serial:10"), QStringLiteral("board-A"),
                              QStringLiteral("PX4 application")};

    void selectApplication(PX4FirmwareUpgradeThreadWorker& worker, const QGCSerialPortInfo& port)
    {
        QGCSerialPortInfo::available = {port};
        worker._setTargetPort(port.location);
        worker._findBoardOnce();
        QGCSerialPortInfo::available.clear();
        worker._findBoardOnce();
    }

private slots:

    void init()
    {
        Bootloader::opened.clear();
        QGCSerialPortInfo::available.clear();
    }

    void normalBoardReenumeratesWithItsSerialNumber()
    {
        PX4FirmwareUpgradeThreadController controller;
        PX4FirmwareUpgradeThreadWorker worker(&controller);
        selectApplication(worker, a);
        QGCSerialPortInfo::available = {{QStringLiteral("harmony-serial:20"), a.serial, QStringLiteral("PX4 BL")}};
        worker._findBoardOnce();
        QCOMPARE(Bootloader::opened, QStringList{QStringLiteral("harmony-serial:20")});
    }

    void differentDeviceCannotReplaceTheSelectedBoard()
    {
        PX4FirmwareUpgradeThreadController controller;
        PX4FirmwareUpgradeThreadWorker worker(&controller);
        QSignalSpy status(&worker, &PX4FirmwareUpgradeThreadWorker::status);
        selectApplication(worker, a);
        QVERIFY(!status.isEmpty());
        QVERIFY(status.first().first().toString().contains(QStringLiteral("select"), Qt::CaseInsensitive));
        QGCSerialPortInfo::available = {{QStringLiteral("harmony-serial:30"), QStringLiteral("board-B"),
                                         QStringLiteral("SiK Radio"), QGCSerialPortInfo::BoardTypeSiKRadio}};
        worker._findBoardOnce();
        QVERIFY(Bootloader::opened.isEmpty());
        QGCSerialPortInfo::available.append({QStringLiteral("harmony-serial:20"), a.serial, QStringLiteral("PX4 BL")});
        worker._findBoardOnce();
        QCOMPARE(Bootloader::opened, QStringList{QStringLiteral("harmony-serial:20")});
    }

    void reusedPortIdCannotOverrideDifferentSerialNumber()
    {
        PX4FirmwareUpgradeThreadController controller;
        PX4FirmwareUpgradeThreadWorker worker(&controller);
        selectApplication(worker, a);
        QGCSerialPortInfo::available = {{a.location, QStringLiteral("board-B"), QStringLiteral("PX4 BL")}};
        worker._findBoardOnce();
        QVERIFY(Bootloader::opened.isEmpty());
    }

    void missingIdentityRequestsAnExplicitReselection()
    {
        PX4FirmwareUpgradeThreadController controller;
        PX4FirmwareUpgradeThreadWorker worker(&controller);
        QSignalSpy errors(&worker, &PX4FirmwareUpgradeThreadWorker::error);
        auto unknown = a;
        unknown.serial.clear();
        selectApplication(worker, unknown);
        QVERIFY(!errors.isEmpty());
        QVERIFY(errors.first().first().toString().contains(QStringLiteral("select"), Qt::CaseInsensitive));
        QVERIFY(worker._targetSystemLocation.isEmpty());
        QGCSerialPortInfo::available = {{a.location, {}, QStringLiteral("PX4 BL")}};
        worker._findBoardOnce();
        QVERIFY(Bootloader::opened.isEmpty());
        worker._setTargetPort(a.location);
        worker._findBoardOnce();
        QCOMPARE(Bootloader::opened, QStringList{a.location});
    }

    void duplicateSerialNumbersRequireReselection()
    {
        PX4FirmwareUpgradeThreadController controller;
        PX4FirmwareUpgradeThreadWorker worker(&controller);
        selectApplication(worker, a);
        QSignalSpy errors(&worker, &PX4FirmwareUpgradeThreadWorker::error);
        QGCSerialPortInfo::available = {{QStringLiteral("harmony-serial:20"), a.serial, QStringLiteral("PX4 BL")},
                                        {QStringLiteral("harmony-serial:30"), a.serial, QStringLiteral("SiK Radio"),
                                         QGCSerialPortInfo::BoardTypeSiKRadio}};
        worker._findBoardOnce();
        QVERIFY(Bootloader::opened.isEmpty());
        QVERIFY(!errors.isEmpty());
        QVERIFY(worker._targetSystemLocation.isEmpty());
    }

    void anExplicitlySelectedBootloaderRemainsUsableWithoutSerialNumber()
    {
        PX4FirmwareUpgradeThreadController controller;
        PX4FirmwareUpgradeThreadWorker worker(&controller);
        QGCSerialPortInfo::available = {{a.location, {}, QStringLiteral("PX4 BL")}};
        worker._setTargetPort(a.location);
        worker._findBoardOnce();
        QCOMPARE(Bootloader::opened, QStringList{a.location});
    }

    void explicitCurrentPortWinsWhenAnotherPortHasTheSameSerial()
    {
        PX4FirmwareUpgradeThreadController controller;
        PX4FirmwareUpgradeThreadWorker worker(&controller);
        QGCSerialPortInfo::available = {{QStringLiteral("harmony-serial:30"), a.serial, QStringLiteral("PX4 BL")},
                                        {a.location, a.serial, QStringLiteral("PX4 BL")}};
        worker._setTargetPort(a.location);
        worker._findBoardOnce();
        QCOMPARE(Bootloader::opened, QStringList{a.location});
    }

    void explicitReselectionCanHandshakeAGenericPortAfterDisconnect()
    {
        PX4FirmwareUpgradeThreadController controller;
        PX4FirmwareUpgradeThreadWorker worker(&controller);
        auto unknown = a;
        unknown.serial.clear();
        selectApplication(worker, unknown);
        QGCSerialPortInfo::available = {{QStringLiteral("harmony-serial:20"), {}, QStringLiteral("USB UART")}};
        worker._findBoardOnce();
        QVERIFY(Bootloader::opened.isEmpty());
        worker._setTargetPort(QStringLiteral("harmony-serial:20"));
        worker._findBoardOnce();
        QCOMPARE(Bootloader::opened, QStringList{QStringLiteral("harmony-serial:20")});
    }

    void placeholderSerialCannotAuthorizeAnAutomaticReplacement()
    {
        PX4FirmwareUpgradeThreadController controller;
        PX4FirmwareUpgradeThreadWorker worker(&controller);
        QSignalSpy errors(&worker, &PX4FirmwareUpgradeThreadWorker::error);
        auto placeholder = a;
        placeholder.serial = QStringLiteral("0");
        selectApplication(worker, placeholder);
        QGCSerialPortInfo::available = {
            {QStringLiteral("harmony-serial:30"), QStringLiteral("0"), QStringLiteral("USB UART")}};
        worker._findBoardOnce();
        QVERIFY(Bootloader::opened.isEmpty());
        QVERIFY(!worker._foundBoard);
        QVERIFY(!errors.isEmpty());
        QVERIFY(worker._targetSystemLocation.isEmpty());
        worker._setTargetPort(QStringLiteral("harmony-serial:30"));
        worker._findBoardOnce();
        QCOMPARE(Bootloader::opened, QStringList{QStringLiteral("harmony-serial:30")});
    }

    void initiallyAmbiguousSerialNeverBecomesAutomaticProof_data()
    {
        QTest::addColumn<bool>("pollSelectedPort");
        QTest::newRow("target-was-observed-before-disconnect") << true;
        QTest::newRow("other-port-alone-at-first-poll") << false;
    }

    void initiallyAmbiguousSerialNeverBecomesAutomaticProof()
    {
        QFETCH(bool, pollSelectedPort);
        PX4FirmwareUpgradeThreadController controller;
        PX4FirmwareUpgradeThreadWorker worker(&controller);
        QSignalSpy errors(&worker, &PX4FirmwareUpgradeThreadWorker::error);
        QGCSerialPortInfo::available = {a, {QStringLiteral("harmony-serial:30"), a.serial, QStringLiteral("USB UART")}};
        worker._setTargetPort(a.location);
        if (pollSelectedPort) {
            worker._findBoardOnce();
            QGCSerialPortInfo::available.clear();
            worker._findBoardOnce();
        }
        QGCSerialPortInfo::available = {{QStringLiteral("harmony-serial:30"), a.serial, QStringLiteral("USB UART")}};
        worker._findBoardOnce();
        QVERIFY(Bootloader::opened.isEmpty());
        QVERIFY(!worker._foundBoard);
        QVERIFY(!errors.isEmpty());
        QVERIFY(worker._targetSystemLocation.isEmpty());
        worker._setTargetPort(QStringLiteral("harmony-serial:30"));
        worker._findBoardOnce();
        QCOMPARE(Bootloader::opened, QStringList{QStringLiteral("harmony-serial:30")});
    }

    void multipleCdcInterfacesAreCountedByPhysicalDevice_data()
    {
        QTest::addColumn<QString>("selectedDevice");
        QTest::addColumn<QString>("otherDevice");
        QTest::addColumn<QString>("serial");
        QTest::addColumn<bool>("automatic");
        QTest::newRow("same-USB-dual-CDC-to-single-bootloader") << "/usb/A" << "/usb/A" << "board-A" << true;
        QTest::newRow("different-USB-devices-with-the-same-serial") << "/usb/A" << "/usb/B" << "board-A" << false;
        QTest::newRow("two-unknown-physical-devices") << "" << "" << "board-A" << false;
        QTest::newRow("one-unknown-physical-device") << "/usb/A" << "" << "board-A" << false;
        QTest::newRow("same-USB-placeholder-serial") << "/usb/A" << "/usb/A" << "0" << false;
    }

    void multipleCdcInterfacesAreCountedByPhysicalDevice()
    {
        QFETCH(QString, selectedDevice);
        QFETCH(QString, otherDevice);
        QFETCH(QString, serial);
        QFETCH(bool, automatic);
        PX4FirmwareUpgradeThreadController controller;
        PX4FirmwareUpgradeThreadWorker worker(&controller);
        QSignalSpy errors(&worker, &PX4FirmwareUpgradeThreadWorker::error);
        auto selected = a;
        selected.physicalDevice = selectedDevice;
        selected.serial = serial;
        auto secondary = selected;
        secondary.location = QStringLiteral("harmony-serial:11");
        secondary.physicalDevice = otherDevice;
        QGCSerialPortInfo::available = {selected, secondary};
        worker._setTargetPort(selected.location);
        worker._findBoardOnce();
        QVERIFY(Bootloader::opened.isEmpty());
        QGCSerialPortInfo::available.clear();
        worker._findBoardOnce();
        auto bootloader = selected;
        bootloader.location = QStringLiteral("harmony-serial:20");
        bootloader.physicalDevice = QStringLiteral("/usb/re-enumerated");
        bootloader.product = QStringLiteral("PX4 BL");
        QGCSerialPortInfo::available = {bootloader};
        worker._findBoardOnce();
        if (automatic) {
            QCOMPARE(Bootloader::opened, QStringList{bootloader.location});
            QVERIFY(errors.isEmpty());
        } else {
            QVERIFY(Bootloader::opened.isEmpty());
            QVERIFY(!worker._foundBoard);
            QVERIFY(!errors.isEmpty());
            QVERIFY(worker._targetSystemLocation.isEmpty());
            worker._setTargetPort(bootloader.location);
            worker._findBoardOnce();
            QCOMPARE(Bootloader::opened, QStringList{bootloader.location});
        }
    }

    void aStalePortSelectionRequestsReselection()
    {
        PX4FirmwareUpgradeThreadController controller;
        PX4FirmwareUpgradeThreadWorker worker(&controller);
        QSignalSpy errors(&worker, &PX4FirmwareUpgradeThreadWorker::error);
        worker._setTargetPort(a.location);
        QVERIFY(!errors.isEmpty());
        QVERIFY(worker._targetSystemLocation.isEmpty());
    }
};
QTEST_GUILESS_MAIN(FirmwarePortSelectionTest)
#include "FirmwarePortSelectionTest.moc"

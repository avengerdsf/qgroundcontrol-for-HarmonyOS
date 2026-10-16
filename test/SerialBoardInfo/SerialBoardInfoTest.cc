#include "BoardFixture.h"

#include <QtTest/QTest>

QList<HarmonySerialPortInfo> HarmonySerialBridge::availablePorts(QString *error, bool *ready)
{
    if (error) { error->clear(); }
    if (ready) { *ready = true; }
    return enumeratedPorts;
}

class SerialBoardInfoTest : public QObject
{
    Q_OBJECT
private slots:
    void init()
    {
        HarmonySerialPortInfo cube{42, QStringLiteral("/usb/42")};
        cube.hasUsbIdentity = true;
        cube.vendorId = 11694;
        cube.productId = 4097;
        cube.productName = QStringLiteral("CubeBlack");
        HarmonySerialPortInfo orange = cube;
        orange.portId = 43;
        orange.productName = QStringLiteral("CubeOrange");
        enumeratedPorts = {cube, orange};
        qgcApp()->messages.clear();
    }

    void classification_data()
    {
        QTest::addColumn<QString>("description");
        QTest::addColumn<bool>("expected");
        QTest::newRow("black") << QStringLiteral("CubeBlack") << true;
        QTest::newRow("black-description") << QStringLiteral("CubeBlack flight controller") << true;
        QTest::newRow("orange") << QStringLiteral("CubeOrange") << false;
        QTest::newRow("case-sensitive-official-rule") << QStringLiteral("cubeblack") << false;
        QTest::newRow("missing-description") << QString() << false;
    }

    void classification()
    {
        QFETCH(QString, description);
        QFETCH(bool, expected);
        enumeratedPorts[0].productName = description;
        QSerialPort port;
        port.setPortName(QStringLiteral("harmony-serial:42"));
        const QGCSerialPortInfo info(port);
        QVERIFY(!info.isNull());
        QCOMPARE(info.description(), description);
        QCOMPARE(info.isBlackCube(), expected);
    }

    void warning_data()
    {
        QTest::addColumn<QString>("portName");
        QTest::addColumn<QVariant>("acc");
        QTest::addColumn<QVariant>("gyr");
        QTest::addColumn<QVariant>("mask");
        QTest::addColumn<bool>("ready");
        QTest::addColumn<int>("linkKind"); // 0 serial, 1 UDP, 2 absent, 3 wrong concrete type
        QTest::addColumn<int>("messages");
        const QString cube = QStringLiteral("harmony-serial:42");
        QTest::newRow("official-bulletin") << cube << QVariant(0) << QVariant(0) << QVariant(7) << true << 0 << 1;
        QTest::newRow("mask-above-threshold") << cube << QVariant(0) << QVariant(0) << QVariant(8) << true << 0 << 1;
        QTest::newRow("mask-below-threshold") << cube << QVariant(0) << QVariant(0) << QVariant(6) << true << 0 << 0;
        QTest::newRow("acc3-present") << cube << QVariant(1) << QVariant(0) << QVariant(7) << true << 0 << 0;
        QTest::newRow("gyr3-present") << cube << QVariant(0) << QVariant(1) << QVariant(7) << true << 0 << 0;
        QTest::newRow("missing-acc3") << cube << QVariant() << QVariant(0) << QVariant(7) << true << 0 << 0;
        QTest::newRow("missing-gyr3") << cube << QVariant(0) << QVariant() << QVariant(7) << true << 0 << 0;
        QTest::newRow("missing-mask") << cube << QVariant(0) << QVariant(0) << QVariant() << true << 0 << 0;
        QTest::newRow("other-enumerated-board") << QStringLiteral("harmony-serial:43") << QVariant(0) << QVariant(0) << QVariant(7) << true << 0 << 0;
        QTest::newRow("port-not-enumerated") << QStringLiteral("harmony-serial:99") << QVariant(0) << QVariant(0) << QVariant(7) << true << 0 << 0;
        QTest::newRow("empty-port") << QString() << QVariant(0) << QVariant(0) << QVariant(7) << true << 0 << 0;
        QTest::newRow("parameters-not-ready") << cube << QVariant(0) << QVariant(0) << QVariant(7) << false << 0 << 0;
        QTest::newRow("udp-link") << cube << QVariant(0) << QVariant(0) << QVariant(7) << true << 1 << 0;
        QTest::newRow("no-primary-link") << cube << QVariant(0) << QVariant(0) << QVariant(7) << true << 2 << 0;
        QTest::newRow("wrong-serial-concrete-type") << cube << QVariant(0) << QVariant(0) << QVariant(7) << true << 3 << 0;
    }

    void warning()
    {
        QFETCH(QString, portName);
        QFETCH(QVariant, acc);
        QFETCH(QVariant, gyr);
        QFETCH(QVariant, mask);
        QFETCH(bool, ready);
        QFETCH(int, linkKind);
        QFETCH(int, messages);
        Vehicle vehicle;
        for (const auto &entry : {qMakePair(QStringLiteral("INS_ACC3_ID"), acc),
                qMakePair(QStringLiteral("INS_GYR3_ID"), gyr), qMakePair(QStringLiteral("INS_ENABLE_MASK"), mask)}) {
            if (entry.second.isValid()) { vehicle.parameters.parameters.insert(entry.first, {entry.second}); }
        }
        auto serial = std::make_shared<SerialConfiguration>();
        serial->name = portName;
        SharedLinkInterfacePtr link;
        if (linkKind == 0) {
            link = std::make_shared<SerialLink>();
            link->configuration = serial;
        } else if (linkKind == 1) {
            link = std::make_shared<LinkInterface>();
            link->configuration = std::make_shared<LinkConfiguration>();
        } else if (linkKind == 3) {
            link = std::make_shared<LinkInterface>();
            link->configuration = serial;
        }
        vehicle.links.primary = link;
        APMAutoPilotPlugin plugin(&vehicle);
        plugin._checkForBadCubeBlack(ready);
        QCOMPARE(qgcApp()->messages.size(), messages);
        if (messages) {
            QVERIFY(qgcApp()->messages.first().contains(QStringLiteral("critical service bulletin")));
            QVERIFY(qgcApp()->messages.first().contains(QStringLiteral("sb-0000002")));
        }
    }
};

QTEST_GUILESS_MAIN(SerialBoardInfoTest)
#include "SerialBoardInfoTest.moc"

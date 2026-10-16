/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "QGCSerialPortInfoTest.h"
#ifndef QGC_NO_SERIAL_LINK
#include "QGCSerialPortInfo.h"
#endif

#include <QtCore/QRegularExpression>
#include <QtTest/QTest>
#if defined(Q_OS_HARMONY) && !defined(QGC_NO_SERIAL_LINK)
#include "HarmonySerialBridge.h"

namespace {
class PortIdentityProvider final : public HarmonySerialProvider
{
public:
    QString error;
    QString pixhawkProductName = QStringLiteral("PX4 BL FMU v2.x");

    QList<HarmonySerialPortInfo> ports(QString *error, bool *ready) override
    {
        if (ready) { *ready = true; }
        if (error) { *error = this->error; }
        if (!this->error.isEmpty()) { return {}; }
        HarmonySerialPortInfo pixhawk{42, QStringLiteral("/usb/42")};
        pixhawk.hasUsbIdentity = true;
        pixhawk.vendorId = 9900;
        pixhawk.productId = 17;
        pixhawk.productName = pixhawkProductName;
        HarmonySerialPortInfo rtk{43, QStringLiteral("/usb/43")};
        rtk.hasUsbIdentity = true;
        rtk.vendorId = 5446;
        rtk.productId = 425;
        HarmonySerialPortInfo missing{44, QStringLiteral("PX4 BL FMU v2.x")};
        HarmonySerialPortInfo unknown{45, QStringLiteral("/usb/45")};
        unknown.hasUsbIdentity = true;
        unknown.vendorId = 1;
        unknown.productId = 2;
        unknown.productName = QStringLiteral("PX4 BL FMU v2.x");
        return {pixhawk, rtk, missing, unknown};
    }
    void call(const Request &, Completion completion) override { completion({}); }
};
struct IdentityProviderGuard {
    std::shared_ptr<PortIdentityProvider> provider = std::make_shared<PortIdentityProvider>();
    IdentityProviderGuard() { HarmonySerialBridge::setTestProvider(provider); }
    ~IdentityProviderGuard() { HarmonySerialBridge::setTestProvider({}); }
};
}
#endif

void QGCSerialPortInfoTest::_testHarmonyEnumerationErrors()
{
#if defined(Q_OS_HARMONY) && !defined(QGC_NO_SERIAL_LINK)
    IdentityProviderGuard guard;
    QCOMPARE(QGCSerialPortInfo::availablePorts().size(), 4);
    QTest::failOnWarning(QRegularExpression(QStringLiteral(".*Harmony USB serial enumeration failed.*")));

    guard.provider->error = QStringLiteral("USB serial capability is unavailable (801)");
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral(".*capability is unavailable \\(801\\).*")));
    QVERIFY(QGCSerialPortInfo::availablePorts().isEmpty());
    QVERIFY(QGCSerialPortInfo::availablePorts().isEmpty());

    guard.provider->error = QStringLiteral("USB serial service failed (31400001)");
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral(".*service failed \\(31400001\\).*")));
    QVERIFY(QGCSerialPortInfo::availablePorts().isEmpty());
    QVERIFY(QGCSerialPortInfo::availablePorts().isEmpty());

    guard.provider->error.clear();
    QCOMPARE(QGCSerialPortInfo::availablePorts().size(), 4);
    guard.provider->error = QStringLiteral("USB serial capability is unavailable (801)");
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral(".*capability is unavailable \\(801\\).*")));
    QVERIFY(QGCSerialPortInfo::availablePorts().isEmpty());
    QVERIFY(QGCSerialPortInfo::availablePorts().isEmpty());
    guard.provider->error.clear();
    QCOMPARE(QGCSerialPortInfo::availablePorts().size(), 4);
#else
    QSKIP("Harmony serial enumeration test requires Harmony serial link support");
#endif
}

void QGCSerialPortInfoTest::_testHarmonyCubeBlack()
{
#if defined(Q_OS_HARMONY) && !defined(QGC_NO_SERIAL_LINK)
    IdentityProviderGuard guard;
    guard.provider->pixhawkProductName = QStringLiteral("CubeBlack");
    const QGCSerialPortInfo cube(QStringLiteral("harmony-serial:42"));
    QCOMPARE(cube.description(), QStringLiteral("CubeBlack"));
    QVERIFY(cube.isBlackCube());
    QVERIFY(!QGCSerialPortInfo(QStringLiteral("harmony-serial:43")).isBlackCube());
    QVERIFY(QGCSerialPortInfo(QStringLiteral("harmony-serial:99")).isNull());
    QVERIFY(!QGCSerialPortInfo(QString()).isBlackCube());

    QSerialPort port;
    port.setPortName(QStringLiteral("harmony-serial:42"));
    QVERIFY(QGCSerialPortInfo(port).isBlackCube());
    guard.provider->pixhawkProductName = QStringLiteral("CubeOrange");
    QVERIFY(!QGCSerialPortInfo(QStringLiteral("harmony-serial:42")).isBlackCube());
    QVERIFY(!QGCSerialPortInfo(port).isBlackCube());
#else
    QSKIP("Harmony CubeBlack test requires Harmony serial link support");
#endif
}

void QGCSerialPortInfoTest::_testLoadJsonData()
{
#ifdef QGC_NO_SERIAL_LINK
    QSKIP("Serial port info test requires serial link support");
#else
    QVERIFY(!QGCSerialPortInfo::_jsonLoaded);
    QVERIFY(QGCSerialPortInfo::_loadJsonData());
    QVERIFY(QGCSerialPortInfo::_jsonLoaded);
    QVERIFY(QGCSerialPortInfo::_jsonDataValid);
    QVERIFY(!QGCSerialPortInfo::_boardInfoList.isEmpty());
    QVERIFY(!QGCSerialPortInfo::_boardDescriptionFallbackList.isEmpty());
    QVERIFY(!QGCSerialPortInfo::_boardManufacturerFallbackList.isEmpty());
#ifdef Q_OS_HARMONY
    IdentityProviderGuard guard;
    const auto ports = QGCSerialPortInfo::availablePorts();
    QCOMPARE(ports.size(), 4);
    QGCSerialPortInfo::BoardType_t type;
    QString name;
    QVERIFY(ports[0].getBoardInfo(type, name));
    QCOMPARE(type, QGCSerialPortInfo::BoardTypePixhawk);
    QCOMPARE(name, QStringLiteral("PX4 FMU V2"));
    QVERIFY(ports[0].canFlash());
    QCOMPARE(ports[0].vendorIdentifier(), quint16(9900));
    QVERIFY(ports[0].serialNumber().isEmpty());
    QVERIFY(ports[1].getBoardInfo(type, name));
    QCOMPARE(type, QGCSerialPortInfo::BoardTypeRTKGPS);
    QVERIFY(!ports[1].canFlash());
    QVERIFY(!ports[2].hasVendorIdentifier());
    QVERIFY(!ports[2].getBoardInfo(type, name));
    QVERIFY(!ports[2].canFlash());
    QVERIFY(!ports[3].getBoardInfo(type, name));
    QVERIFY(!ports[3].canFlash());
#endif
#endif
}

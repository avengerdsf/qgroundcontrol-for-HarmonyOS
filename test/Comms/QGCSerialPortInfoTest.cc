#include "QGCSerialPortInfoTest.h"

#include <QtCore/QRegularExpression>
#include <QtTest/QTest>

#include "QGCSerialPortInfo.h"
#if defined(Q_OS_HARMONY) && !defined(QGC_NO_SERIAL_LINK)
#include "HarmonySerialBridge.h"

namespace {
class PortIdentityProvider final : public HarmonySerialProvider
{
public:
    QString error;
    QString pixhawkProductName = QStringLiteral("PX4 BL FMU v2.x");

    QList<HarmonySerialPortInfo> ports(QString* errorOutput, bool* ready) override
    {
        if (ready) {
            *ready = true;
        }
        if (errorOutput) {
            *errorOutput = error;
        }
        if (!this->error.isEmpty()) {
            return {};
        }
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

    void call(const Request&, Completion completion) override { completion({}); }
};

struct IdentityProviderGuard
{
    std::shared_ptr<PortIdentityProvider> provider = std::make_shared<PortIdentityProvider>();

    IdentityProviderGuard() { HarmonySerialBridge::setTestProvider(provider); }

    ~IdentityProviderGuard() { HarmonySerialBridge::setTestProvider({}); }
};
}  // namespace
#endif

void QGCSerialPortInfoTest::_testHarmonyEnumerationErrors()
{
#if defined(Q_OS_HARMONY) && !defined(QGC_NO_SERIAL_LINK)
    IdentityProviderGuard guard;
    QCOMPARE(QGCSerialPortInfo::availablePorts().size(), 4);

    guard.provider->error = QStringLiteral("USB serial capability is unavailable (801)");
    expectLogMessage("Comms.QGCSerialPortInfo", QtWarningMsg,
                     QRegularExpression(QStringLiteral(".*capability is unavailable \\(801\\).*")));
    QVERIFY(QGCSerialPortInfo::availablePorts().isEmpty());
    verifyExpectedLogMessage();
    QVERIFY(QGCSerialPortInfo::availablePorts().isEmpty());

    guard.provider->error = QStringLiteral("USB serial service failed (31400001)");
    expectLogMessage("Comms.QGCSerialPortInfo", QtWarningMsg,
                     QRegularExpression(QStringLiteral(".*service failed \\(31400001\\).*")));
    QVERIFY(QGCSerialPortInfo::availablePorts().isEmpty());
    verifyExpectedLogMessage();
    QVERIFY(QGCSerialPortInfo::availablePorts().isEmpty());

    guard.provider->error.clear();
    QCOMPARE(QGCSerialPortInfo::availablePorts().size(), 4);
    guard.provider->error = QStringLiteral("USB serial capability is unavailable (801)");
    expectLogMessage("Comms.QGCSerialPortInfo", QtWarningMsg,
                     QRegularExpression(QStringLiteral(".*capability is unavailable \\(801\\).*")));
    QVERIFY(QGCSerialPortInfo::availablePorts().isEmpty());
    verifyExpectedLogMessage();
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

void QGCSerialPortInfoTest::_testLoadJsonDataIdempotent()
{
    QVERIFY(QGCSerialPortInfo::_loadJsonData());
    const int boardCount = QGCSerialPortInfo::_boardInfoList.count();
    const int descriptionFallbackCount = QGCSerialPortInfo::_boardDescriptionFallbackList.count();
    const int manufacturerFallbackCount = QGCSerialPortInfo::_boardManufacturerFallbackList.count();

    QVERIFY(QGCSerialPortInfo::_loadJsonData());
    QCOMPARE(QGCSerialPortInfo::_boardInfoList.count(), boardCount);
    QCOMPARE(QGCSerialPortInfo::_boardDescriptionFallbackList.count(), descriptionFallbackCount);
    QCOMPARE(QGCSerialPortInfo::_boardManufacturerFallbackList.count(), manufacturerFallbackCount);
}

void QGCSerialPortInfoTest::_testBoardClassStringToType()
{
    QCOMPARE(QGCSerialPortInfo::_boardClassStringToType(QStringLiteral("Pixhawk")),
             QGCSerialPortInfo::BoardTypePixhawk);
    QCOMPARE(QGCSerialPortInfo::_boardClassStringToType(QStringLiteral("SiK Radio")),
             QGCSerialPortInfo::BoardTypeSiKRadio);
    QCOMPARE(QGCSerialPortInfo::_boardClassStringToType(QStringLiteral("OpenPilot")),
             QGCSerialPortInfo::BoardTypeOpenPilot);
    QCOMPARE(QGCSerialPortInfo::_boardClassStringToType(QStringLiteral("RTK GPS")),
             QGCSerialPortInfo::BoardTypeRTKGPS);
    QCOMPARE(QGCSerialPortInfo::_boardClassStringToType(QStringLiteral("UnknownClass")),
             QGCSerialPortInfo::BoardTypeUnknown);
}

void QGCSerialPortInfoTest::_testBoardTypeToString()
{
    QCOMPARE(QGCSerialPortInfo::_boardTypeToString(QGCSerialPortInfo::BoardTypePixhawk),
             QStringLiteral("Pixhawk"));
    QCOMPARE(QGCSerialPortInfo::_boardTypeToString(QGCSerialPortInfo::BoardTypeSiKRadio),
             QStringLiteral("SiK Radio"));
    QCOMPARE(QGCSerialPortInfo::_boardTypeToString(QGCSerialPortInfo::BoardTypeOpenPilot),
             QStringLiteral("OpenPilot"));
    QCOMPARE(QGCSerialPortInfo::_boardTypeToString(QGCSerialPortInfo::BoardTypeRTKGPS),
             QStringLiteral("RTK GPS"));
    QCOMPARE(QGCSerialPortInfo::_boardTypeToString(QGCSerialPortInfo::BoardTypeUnknown),
             QStringLiteral("Unknown"));
}

void QGCSerialPortInfoTest::_testBoardClassStringToTypeCaseInsensitivity()
{
    // String-to-type lookup is expected to treat whitespace/case gracefully on
    // the Unknown fallback path: anything that doesn't exactly match a known
    // class string must resolve to BoardTypeUnknown rather than asserting.
    QCOMPARE(QGCSerialPortInfo::_boardClassStringToType(QString()),
             QGCSerialPortInfo::BoardTypeUnknown);
    QCOMPARE(QGCSerialPortInfo::_boardClassStringToType(QStringLiteral(" ")),
             QGCSerialPortInfo::BoardTypeUnknown);
    QCOMPARE(QGCSerialPortInfo::_boardClassStringToType(QStringLiteral("pixhawk")),
             QGCSerialPortInfo::BoardTypeUnknown);  // case-sensitive today
}

void QGCSerialPortInfoTest::_testBoardTypeStringRoundTrip()
{
    // For every known board type, `_boardTypeToString` -> `_boardClassStringToType`
    // must round-trip exactly. This pins the invariant that the two lookup
    // tables stay in sync when either is extended.
    const QGCSerialPortInfo::BoardType_t types[] = {
        QGCSerialPortInfo::BoardTypePixhawk,
        QGCSerialPortInfo::BoardTypeSiKRadio,
        QGCSerialPortInfo::BoardTypeOpenPilot,
        QGCSerialPortInfo::BoardTypeRTKGPS,
    };
    for (QGCSerialPortInfo::BoardType_t t : types) {
        const QString s = QGCSerialPortInfo::_boardTypeToString(t);
        QVERIFY2(!s.isEmpty(), "_boardTypeToString must return non-empty for known types");
        QCOMPARE(QGCSerialPortInfo::_boardClassStringToType(s), t);
    }
}

void QGCSerialPortInfoTest::_testBoardInfoListEntriesAreWellFormed()
{
    QVERIFY(QGCSerialPortInfo::_loadJsonData());

    for (const auto& entry : QGCSerialPortInfo::_boardInfoList) {
        // VID/PID are 16-bit USB identifiers; negative or >0xFFFF would mean
        // the JSON parser produced junk.
        QVERIFY2(entry.vendorId  >= 0 && entry.vendorId  <= 0xFFFF,
                 qPrintable(QStringLiteral("vendorId out of range for %1").arg(entry.name)));
        QVERIFY2(entry.productId >= 0 && entry.productId <= 0xFFFF,
                 qPrintable(QStringLiteral("productId out of range for %1").arg(entry.name)));
        QVERIFY2(!entry.name.isEmpty(), "board entry missing name");
        QVERIFY2(entry.boardType != QGCSerialPortInfo::BoardTypeUnknown,
                 qPrintable(QStringLiteral("board %1 has BoardTypeUnknown class")
                                .arg(entry.name)));
    }
}

void QGCSerialPortInfoTest::_testFallbackRegexesCompile()
{
    QVERIFY(QGCSerialPortInfo::_loadJsonData());

    // Any invalid regex in the JSON would make runtime detection silently
    // fail, so assert compile-time validity here instead.
    for (const auto& entry : QGCSerialPortInfo::_boardDescriptionFallbackList) {
        QVERIFY2(entry.regExp.isValid(),
                 qPrintable(QStringLiteral("invalid description regex: %1")
                                .arg(entry.regExp.pattern())));
    }
    for (const auto& entry : QGCSerialPortInfo::_boardManufacturerFallbackList) {
        QVERIFY2(entry.regExp.isValid(),
                 qPrintable(QStringLiteral("invalid manufacturer regex: %1")
                                .arg(entry.regExp.pattern())));
    }
}

UT_REGISTER_TEST(QGCSerialPortInfoTest, TestLabel::Unit, TestLabel::Comms)

#include "HarmonyNmeaTest.h"
#include "PositionManager.h"
#include "UdpIODevice.h"

#include <QtNetwork/QHostAddress>
#include <QtNetwork/QUdpSocket>
#include <QtPositioning/QNmeaPositionInfoSource>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

namespace {
QByteArray sentence(const QByteArray &body)
{
    quint8 checksum = 0;
    for (const char character : body) { checksum ^= static_cast<quint8>(character); }
    return QByteArrayLiteral("$") + body + '*' + QByteArray::number(checksum, 16).rightJustified(2, '0').toUpper() + "\r\n";
}

QByteArray fix(const QByteArray &time, const QByteArray &latitude = QByteArrayLiteral("4807.038"),
    const QByteArray &longitude = QByteArrayLiteral("01131.000"))
{
    return sentence(QByteArrayLiteral("GPRMC,") + time + ",A," + latitude + ",N," + longitude + ",E,0.0,0.0,131026,,,A") +
        sentence(QByteArrayLiteral("GPGGA,") + time + ',' + latitude + ",N," + longitude + ",E,1,08,0.9,545.4,M,46.9,M,,");
}

struct NmeaFixture
{
    bool initialize()
    {
        if (!device.bind(QHostAddress(QHostAddress::LocalHost), 0)) { return false; }
        manager.setNmeaSourceDevice(&device);
        source = manager.findChild<QNmeaPositionInfoSource *>();
        if (!source) { return false; }
        source->setUpdateInterval(50);
        return true;
    }

    qint64 send(const QByteArray &data)
    {
        return sender.writeDatagram(data, QHostAddress(QHostAddress::LocalHost), device.localPort());
    }

    UdpIODevice device;
    QGCPositionManager manager;
    QUdpSocket sender;
    QNmeaPositionInfoSource *source = nullptr;
};
}

void HarmonyNmeaTest::_udpRmcGgaPosition()
{
    NmeaFixture fixture;
    QVERIFY(fixture.initialize());
    QCOMPARE(fixture.source->device(), &fixture.device);
    QCOMPARE(fixture.device.state(), QAbstractSocket::BoundState);
    QSignalSpy datagrams(&fixture.device, &QIODevice::readyRead);
    QSignalSpy updates(&fixture.manager, &QGCPositionManager::positionInfoUpdated);
    QSignalSpy positions(&fixture.manager, &QGCPositionManager::gcsPositionChanged);
    const QByteArray data = fix(QByteArrayLiteral("120001.000"));
    QCOMPARE(fixture.send(data.first(7)), qint64(7));
    QTRY_VERIFY_WITH_TIMEOUT(datagrams.count() > 0, 1000);
    QVERIFY(!fixture.device.canReadLine());
    QCOMPARE(updates.count(), 0);
    QCOMPARE(fixture.send(data.mid(7)), qint64(data.size() - 7));
    QTRY_VERIFY_WITH_TIMEOUT(updates.count() > 0, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(positions.count() > 0, 1000);
    const QGeoPositionInfo info = fixture.manager.geoPositionInfo();
    QVERIFY(info.isValid());
    QVERIFY(qAbs(info.coordinate().latitude() - 48.1173) < 1e-7);
    QVERIFY(qAbs(info.coordinate().longitude() - (11.0 + 31.0 / 60.0)) < 1e-7);
    QVERIFY(qAbs(info.coordinate().altitude() - 545.4) < 1e-7);
    QCOMPARE(info.timestamp().date(), QDate(2026, 10, 13));
    QCOMPARE(info.timestamp().time(), QTime(12, 0, 1));
    QVERIFY(qAbs(fixture.manager.gcsPosition().latitude() - 48.1173) < 1e-7);
    QVERIFY(qAbs(fixture.manager.gcsPosition().longitude() - (11.0 + 31.0 / 60.0)) < 1e-7);
    QVERIFY(fixture.manager.gcsPositionHorizontalAccuracy() > 0.0);
    QVERIFY(fixture.manager.gcsPositionHorizontalAccuracy() < 100.0);
}

void HarmonyNmeaTest::_invalidChecksumDoesNotUpdate()
{
    NmeaFixture fixture;
    QVERIFY(fixture.initialize());
    QSignalSpy updates(&fixture.manager, &QGCPositionManager::positionInfoUpdated);
    const QByteArray initial = fix(QByteArrayLiteral("120001.000"));
    QCOMPARE(fixture.send(initial), qint64(initial.size()));
    QTRY_VERIFY_WITH_TIMEOUT(updates.count() > 0, 2000);
    const QGeoPositionInfo previous = fixture.manager.geoPositionInfo();
    updates.clear();
    QByteArray invalid = fix(QByteArrayLiteral("120002.000"), QByteArrayLiteral("4907.038"), QByteArrayLiteral("01231.000"));
    for (qsizetype star = invalid.indexOf('*'); star >= 0; star = invalid.indexOf('*', star + 3)) {
        invalid[star + 1] = invalid[star + 1] == '0' ? '1' : '0';
    }
    QSignalSpy datagrams(&fixture.device, &QIODevice::readyRead);
    QCOMPARE(fixture.send(invalid), qint64(invalid.size()));
    QTRY_VERIFY_WITH_TIMEOUT(datagrams.count() > 0, 1000);
    QTest::qWait(250);
    QCOMPARE(updates.count(), 0);
    QCOMPARE(fixture.manager.geoPositionInfo(), previous);
}

void HarmonyNmeaTest::_stopAndRestartUpdates()
{
    NmeaFixture fixture;
    QVERIFY(fixture.initialize());
    QSignalSpy updates(&fixture.manager, &QGCPositionManager::positionInfoUpdated);
    const QByteArray initial = fix(QByteArrayLiteral("120001.000"));
    QCOMPARE(fixture.send(initial), qint64(initial.size()));
    QTRY_VERIFY_WITH_TIMEOUT(updates.count() > 0, 2000);
    const QGeoPositionInfo previous = fixture.manager.geoPositionInfo();
    fixture.source->stopUpdates();
    updates.clear();
    QSignalSpy datagrams(&fixture.device, &QIODevice::readyRead);
    const QByteArray stopped = fix(QByteArrayLiteral("120002.000"), QByteArrayLiteral("4907.038"), QByteArrayLiteral("01231.000"));
    QCOMPARE(fixture.send(stopped), qint64(stopped.size()));
    QTRY_VERIFY_WITH_TIMEOUT(datagrams.count() > 0, 1000);
    QTest::qWait(250);
    QCOMPARE(updates.count(), 0);
    QCOMPARE(fixture.manager.geoPositionInfo(), previous);
    fixture.source->startUpdates();
    const QByteArray restarted = fix(QByteArrayLiteral("120003.000"), QByteArrayLiteral("5007.038"), QByteArrayLiteral("01331.000"));
    QCOMPARE(fixture.send(restarted), qint64(restarted.size()));
    QTRY_VERIFY_WITH_TIMEOUT(updates.count() > 0, 2000);
    QVERIFY(qAbs(fixture.manager.gcsPosition().latitude() - 50.1173) < 1e-7);
    QVERIFY(qAbs(fixture.manager.gcsPosition().longitude() - (13.0 + 31.0 / 60.0)) < 1e-7);
    QCOMPARE(fixture.manager.geoPositionInfo().timestamp().time(), QTime(12, 0, 3));
}

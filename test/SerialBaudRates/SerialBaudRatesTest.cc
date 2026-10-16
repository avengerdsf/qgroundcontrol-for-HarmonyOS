#include "RatesFixture.h"

#include <QtTest/QTest>

class SerialBaudRatesTest : public QObject
{
    Q_OBJECT

private slots:
    void matchesOfficialV508_data()
    {
        QTest::addColumn<QList<qint32>>("qtRates");
        QTest::newRow("empty-qt-list") << QList<qint32>{};
        QTest::newRow("duplicate-qt-rates") << QList<qint32>{115200, 9600, 115200, 9600};
        QTest::newRow("harmony-sdk-rates") << QList<qint32>{50, 75, 110, 134, 150, 200, 300, 600,
            1200, 1800, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800,
            500000, 576000, 921600, 1000000, 1152000, 1500000, 2000000, 2500000,
            3000000, 3500000, 4000000};
        QTest::newRow("unsorted-extra-duplicates") << QList<qint32>{4000000, 12345, 9600, 12345, 4000000};
    }

    void matchesOfficialV508()
    {
        QFETCH(QList<qint32>, qtRates);
        QSerialPortInfo::qtRates = {};
        QSerialPortInfo::calls = 0;
        const QStringList expected = UpstreamSerialConfiguration::supportedBaudRates();
        QCOMPARE(QSerialPortInfo::calls, 1);
        QCOMPARE(expected.size(), 19);

        QSerialPortInfo::qtRates = qtRates;
        QSerialPortInfo::calls = 0;
        const QStringList actual = SerialConfiguration::supportedBaudRates();
        QCOMPARE(actual, expected);
        QCOMPARE(QSerialPortInfo::calls, 0);
    }

    void nativeBridgeCoversOfficialList()
    {
        QSerialPortInfo::qtRates = {};
        const QStringList expected = UpstreamSerialConfiguration::supportedBaudRates();
        const QList<qint32> accepted = HarmonySerialBridge::supportedBaudRates();
        for (const QString &rate : expected) {
            QVERIFY2(accepted.contains(rate.toInt()), qPrintable(rate));
        }
    }
};

QTEST_GUILESS_MAIN(SerialBaudRatesTest)
#include "SerialBaudRatesTest.moc"

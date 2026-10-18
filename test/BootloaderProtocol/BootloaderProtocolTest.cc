#include <QtCore/QTemporaryDir>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "Fixture.h"

namespace {
QByteArray imageBytes()
{
    QByteArray bytes(65540, Qt::Uninitialized);
    for (qsizetype index = 0; index < bytes.size(); ++index) {
        bytes[index] = char((index * 73 + (index >> 8) * 29 + (index >> 16) * 17) & 0xff);
    }
    return bytes;
}

bool saveImage(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

quint32 referenceCrc(const QByteArray& bytes)
{
    quint32 crc = 0;
    for (const char byte : bytes) {
        crc ^= quint8(byte);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0U);
        }
    }
    return crc;
}
}  // namespace

class BootloaderProtocolTest : public QObject
{
    Q_OBJECT
private slots:

    void completeFrameSurvivesCompletionDelay()
    {
        QTemporaryDir scratch;
        QVERIFY(scratch.isValid());
        const QByteArray bytes = imageBytes();
        QVERIFY(bytes.size() > 65536);
        QVERIFY(bytes.first(256) != bytes.mid(256, 256));
        FirmwareImage image{scratch.filePath(QStringLiteral("firmware.bin"))};
        QVERIFY(saveImage(image.filename, bytes));
        Bootloader bootloader;
        QSignalSpy progress(&bootloader, &Bootloader::updateProgress);
        QVERIFY2(bootloader._binProgram(&image), qPrintable(bootloader._errorString));
        QCOMPARE(bootloader._port.programmed, bytes);
        QCOMPARE(bootloader._port.frames.size(), (bytes.size() + 63) / 64);
        QCOMPARE(bootloader._port.requests.size(), bootloader._port.frames.size());
        for (const QByteArray& frame : bootloader._port.frames) {
            QCOMPARE(quint8(frame[0]), quint8(0x27));
            QVERIFY(quint8(frame[1]) <= 64);
            QCOMPARE(quint8(frame[1]) % 4, 0);
            QCOMPARE(frame.size(), qsizetype(quint8(frame[1]) + 3));
            QCOMPARE(quint8(frame.back()), quint8(0x20));
        }
        QCOMPARE(bootloader._port.requests.first().size(), qsizetype(67));
        QCOMPARE(bootloader._port.requests.last().size(), qsizetype(7));
        QByteArray padded = bytes;
        padded.append(QByteArray(bootloader._boardFlashSize - bytes.size(), char(0xff)));
        QCOMPARE(bootloader._imageCRC, referenceCrc(padded));
        QCOMPARE(progress.last().at(0).toInt(), bytes.size());
    }

    void shortWriteIsRejected_data()
    {
        QTest::addColumn<int>("shortBy");
        QTest::newRow("missing-eoc") << 1;
        QTest::newRow("missing-payload-tail-and-eoc") << 2;
        QTest::newRow("header-only") << 65;
        QTest::newRow("zero-write") << 67;
    }

    void invalidReplyRetainsExactOffset()
    {
        QTemporaryDir scratch;
        QVERIFY(scratch.isValid());
        const QByteArray bytes = imageBytes();
        FirmwareImage image{scratch.filePath(QStringLiteral("firmware.bin"))};
        QVERIFY(saveImage(image.filename, bytes));
        Bootloader bootloader;
        bootloader._port.rejectAt = 0xf980;
        QSignalSpy progress(&bootloader, &Bootloader::updateProgress);
        QVERIFY(!bootloader._binProgram(&image));
        QVERIFY(bootloader._errorString.contains(QStringLiteral("0x13 (PROTO_INVALID)")));
        QVERIFY(bootloader._errorString.endsWith(QStringLiteral("at address 0x0000f980")));
        QCOMPARE(bootloader._port.programmed, bytes.first(0xf980));
        QCOMPARE(progress.count(), 0xf980 / 64);
        QCOMPARE(bootloader._port.requests.size(), 0xf980 / 64 + 1);
    }

    void shortWriteIsRejected()
    {
        QFETCH(int, shortBy);
        QTemporaryDir scratch;
        QVERIFY(scratch.isValid());
        FirmwareImage image{scratch.filePath(QStringLiteral("firmware.bin"))};
        QVERIFY(saveImage(image.filename, imageBytes()));
        Bootloader bootloader;
        bootloader._port.shortBy = shortBy;
        QVERIFY(!bootloader._binProgram(&image));
        QVERIFY(bootloader._errorString.contains(QStringLiteral("Incorrect number of bytes")));
        QVERIFY(bootloader._port.programmed.isEmpty());
        QCOMPARE(bootloader._imageCRC, quint32(0));
        QCOMPARE(bootloader._port.requests.size(), 1);
    }
};
QTEST_GUILESS_MAIN(BootloaderProtocolTest)
#include "BootloaderProtocolTest.moc"

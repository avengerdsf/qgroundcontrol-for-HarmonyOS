#include "Fixture.h"
#include <QtCore/QCryptographicHash>
#include <QtCore/QTemporaryDir>
#include <QtTest/QTest>

namespace {
QByteArray hashFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { return {}; }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&file) ? hash.result() : QByteArray{};
}
bool writePayload(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) { return false; }
    QByteArray block(32768, Qt::Uninitialized);
    for (qsizetype i = 0; i < block.size(); ++i) { block[i] = char(i % 251); }
    for (int i = 0; i < 97; ++i) { if (file.write(block) != block.size()) { return false; } }
    return file.write("tail\0bytes", 10) == 10 && file.flush();
}
QStringList outputFiles(const QString &directory)
{
    return QDir(directory).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
}
}

class TelemetrySaveTest : public QObject
{
    Q_OBJECT
private slots:
    void init()
    {
        IoFaults::reset();
        qgcApp()->messages.clear();
    }

    void multiBlockCopyHandlesShortWrites()
    {
        QTemporaryDir directory(QStringLiteral(TEST_BINARY_DIR "/copy-XXXXXX"));
        QVERIFY(directory.isValid());
        const QString output = directory.filePath("Telemetry");
        QVERIFY(QDir().mkpath(output));
        SettingsManager::instance()->settings.directory = output;
        const QString source = directory.filePath("FlightData.mavlink");
        QVERIFY(writePayload(source));
        const qint64 size = QFileInfo(source).size();
        const QByteArray hash = hashFile(source);
        QVERIFY(!hash.isEmpty());
        MAVLinkProtocol()._saveTelemetryLog(source);
        QVERIFY2(qgcApp()->messages.isEmpty(), qPrintable(qgcApp()->messages.join('\n')));
        QVERIFY(!QFile::exists(source));
        QCOMPARE(outputFiles(output), QStringList{"2026-10-15 12-34-56.tlog"});
        const QString saved = QDir(output).filePath(outputFiles(output).first());
        QCOMPARE(QFileInfo(saved).size(), size);
        QCOMPARE(hashFile(saved), hash);
        QCOMPARE(IoFaults::copies, 0);
        QVERIFY(IoFaults::reads > 2 && IoFaults::writes > 2);
        QVERIFY(IoFaults::maxReadRequest <= 65536 && IoFaults::maxWriteRequest <= 65536);
    }

    void failedIoRetainsSourceAndCleansOutput_data()
    {
        QTest::addColumn<QString>("failure");
        for (const char *name : {"read", "write", "zero-write", "flush"}) {
            QTest::newRow(name) << QString::fromLatin1(name);
        }
    }
    void failedIoRetainsSourceAndCleansOutput()
    {
        QFETCH(QString, failure);
        QTemporaryDir directory(QStringLiteral(TEST_BINARY_DIR "/failure-XXXXXX"));
        QVERIFY(directory.isValid());
        const QString output = directory.filePath("Telemetry");
        QVERIFY(QDir().mkpath(output));
        SettingsManager::instance()->settings.directory = output;
        const QString source = directory.filePath("FlightData.mavlink");
        QVERIFY(writePayload(source));
        const QByteArray hash = hashFile(source);
        if (failure == "read") { IoFaults::failReadAfter = 65536; }
        if (failure == "write") { IoFaults::failWriteAfter = 65536; }
        if (failure == "zero-write") { IoFaults::zeroWrite = true; }
        if (failure == "flush") { IoFaults::failFlush = true; }
        MAVLinkProtocol()._saveTelemetryLog(source);
        QCOMPARE(hashFile(source), hash);
        QVERIFY(outputFiles(output).isEmpty());
        QCOMPARE(qgcApp()->messages.size(), 1);
        QCOMPARE(IoFaults::copies, 0);
        QVERIFY(IoFaults::reads > 0);
        if (failure != "read") { QVERIFY(IoFaults::writes > 0); }
        if (failure == "zero-write") { QVERIFY(IoFaults::writes < 5); }
    }

    void existingFileKeepsOfficialSuffix()
    {
        QTemporaryDir directory(QStringLiteral(TEST_BINARY_DIR "/existing-XXXXXX"));
        QVERIFY(directory.isValid());
        const QString output = directory.filePath("Telemetry");
        QVERIFY(QDir().mkpath(output));
        SettingsManager::instance()->settings.directory = output;
        const QString original = QDir(output).filePath("2026-10-15 12-34-56.tlog");
        QVERIFY(writePayload(original));
        const QByteArray originalHash = hashFile(original);
        const QString source = directory.filePath("FlightData.mavlink");
        QVERIFY(writePayload(source));
        const QByteArray hash = hashFile(source);
        MAVLinkProtocol()._saveTelemetryLog(source);
        QVERIFY(!QFile::exists(source));
        QCOMPARE(hashFile(original), originalHash);
        QCOMPARE(hashFile(QDir(output).filePath("2026-10-15 12-34-56.1.tlog")), hash);
        QCOMPARE(outputFiles(output).size(), 2);
    }

    void publicationRaceNeverOverwrites()
    {
        QTemporaryDir directory(QStringLiteral(TEST_BINARY_DIR "/race-XXXXXX"));
        QVERIFY(directory.isValid());
        const QString output = directory.filePath("Telemetry");
        QVERIFY(QDir().mkpath(output));
        SettingsManager::instance()->settings.directory = output;
        const QString source = directory.filePath("FlightData.mavlink");
        QVERIFY(writePayload(source));
        const QByteArray hash = hashFile(source);
        IoFaults::collideAtPublish = true;
        MAVLinkProtocol()._saveTelemetryLog(source);
        QVERIFY(!IoFaults::collisionPath.isEmpty());
        QCOMPARE(hashFile(source), hash);
        QCOMPARE(outputFiles(output).size(), 1);
        QFile collision(IoFaults::collisionPath);
        QVERIFY(collision.open(QIODevice::ReadOnly));
        QCOMPARE(collision.readAll(), QByteArray("existing-race-file"));
        QCOMPARE(qgcApp()->messages.size(), 1);
    }

    void padBackupContents_data()
    {
        QTest::addColumn<QString>("backup");
        const QDir directory(QStringLiteral(BACKUP_DIR));
        const QStringList files = directory.entryList({"FlightData*.mavlink"}, QDir::Files);
        QCOMPARE(files.size(), 4);
        for (const QString &file : files) { QTest::newRow(qPrintable(file)) << directory.filePath(file); }
    }
    void padBackupContents()
    {
        QFETCH(QString, backup);
        const QByteArray originalHash = hashFile(backup);
        QVERIFY(!originalHash.isEmpty());
        QTemporaryDir directory(QStringLiteral(TEST_BINARY_DIR "/backup-XXXXXX"));
        QVERIFY(directory.isValid());
        const QString output = directory.filePath("Telemetry");
        QVERIFY(QDir().mkpath(output));
        SettingsManager::instance()->settings.directory = output;
        const QString source = directory.filePath("FlightData.mavlink");
        QVERIFY(QFile::copy(backup, source));
        MAVLinkProtocol()._saveTelemetryLog(source);
        QVERIFY(!QFile::exists(source));
        QCOMPARE(outputFiles(output).size(), 1);
        const QString saved = QDir(output).filePath(outputFiles(output).first());
        QCOMPARE(QFileInfo(saved).size(), QFileInfo(backup).size());
        QCOMPARE(hashFile(saved), originalHash);
        QCOMPARE(hashFile(backup), originalHash);
        QVERIFY(IoFaults::reads > 2);
        QCOMPARE(IoFaults::copies, 0);
    }
};
QTEST_GUILESS_MAIN(TelemetrySaveTest)
#include "TelemetrySaveTest.moc"

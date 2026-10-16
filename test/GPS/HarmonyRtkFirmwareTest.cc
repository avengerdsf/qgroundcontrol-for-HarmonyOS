#include "HarmonyRtkFirmwareTest.h"
#include "HarmonySerialIODevice.h"
#include "Bootloader.h"
#include "FirmwareImage.h"
#include "FirmwareUpgradeController.h"
#include "Fact.h"
#include "QGCApplication.h"
#include "GPSProvider.h"
#include "GPSRtk.h"
#include "PX4FirmwareUpgradeThread.h"
#include <QtCore/QElapsedTimer>
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QMutexLocker>
#include <QtCore/QPointer>
#include <QtCore/QScopeGuard>
#include <QtCore/QStandardPaths>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtCore/QtEndian>
#include <QtTest/QTest>
#include <QtTest/QSignalSpy>
#include <future>

namespace {
QString testDirectoryTemplate()
{
#ifdef Q_OS_HARMONY
    const QString root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/tmp");
#else
    const QString root = QFileInfo(QString::fromUtf8(__FILE__)).dir().absoluteFilePath(QStringLiteral("../../tmp"));
#endif
    (void) QDir().mkpath(root);
    return root + QStringLiteral("/qgc-firmware-XXXXXX");
}

// The device boundary is controlled; Bootloader and the transport remain real.
class ProtocolProvider final : public HarmonySerialProvider
{
public:
    QList<HarmonySerialPortInfo> ports(QString *, bool *ready) override
    {
        if (ready) { *ready = true; }
        return metadata;
    }

    void call(const Request &request, Completion completion) override
    {
        ++ioCalls;
        Completion readCompletion;
        Result readResult;
        Result result;
        {
            const QMutexLocker locker(&mutex);
            if (request.operation == Operation::SetBaud && holdBaud) {
                pendingBaud = std::move(completion);
                return;
            }
            if (request.operation == Operation::Read) {
                pendingRead = std::move(completion);
            } else if (request.operation == Operation::Write) {
                // Partial native writes exercise the bridge's byte ordering.
                result.written = zeroWrite ? 0 : delayedUsbCompletion ? request.data.size() :
                    qMin<qint64>(3, request.data.size());
                if (delayedUsbCompletion && input.isEmpty() && !request.data.isEmpty() &&
                    quint8(request.data[0]) == 0x27) {
                    programWriteSizes.append(request.data.size());
                }
                input.append(request.data.first(result.written));
                transmitted.append(request.data.first(result.written));
                if (!rawMode) { parseCommands(); }
            } else if (request.operation == Operation::Close) {
                ++closes;
            } else {
                baud = request.settings.baud;
                if (request.operation == Operation::Open) { automatic = request.settings.autoConnect; }
            }
            if (pendingRead && (!output.isEmpty() || unplugged)) {
                readCompletion = std::move(pendingRead);
                if (unplugged) {
                    readResult.errorCode = unplugErrorCode;
                    readResult.error = unplugError;
                } else {
                    readResult.data = output.first(1);
                    output.remove(0, 1);
                }
            }
        }
        if (request.operation == Operation::Write && delayedUsbCompletion) {
            // Bytes reach the peer before the asynchronous USB completion.
            QTimer::singleShot(60, [completion = std::move(completion), result]() { completion(result); });
        } else if (request.operation != Operation::Read) {
            completion(result);
        }
        if (readCompletion) { readCompletion(std::move(readResult)); }
    }

    void receive(const QByteArray &data)
    {
        Completion completion;
        Result result;
        {
            const QMutexLocker locker(&mutex);
            output.append(data);
            if (pendingRead && !output.isEmpty()) {
                completion = std::move(pendingRead);
                result.data = output.first(1);
                output.remove(0, 1);
            }
        }
        if (completion) { completion(std::move(result)); }
    }

    QByteArray programmedBytes() const
    {
        const QMutexLocker locker(&mutex);
        return programmed;
    }
    QByteArray transmittedBytes() const { const QMutexLocker locker(&mutex); return transmitted; }
    qint32 configuredBaud() const { const QMutexLocker locker(&mutex); return baud; }
    bool autoConnectRequested() const { const QMutexLocker locker(&mutex); return automatic; }
    void releaseBaud()
    {
        Completion completion;
        {
            const QMutexLocker locker(&mutex);
            holdBaud = false;
            completion = std::move(pendingBaud);
        }
        if (completion) { completion({}); }
    }
    void unplug(int errorCode = 31400003,
                const QString &error = QStringLiteral("USB serial device unplugged"))
    {
        Completion completion;
        Result result;
        {
            const QMutexLocker locker(&mutex);
            unplugged = true;
            unplugErrorCode = errorCode;
            unplugError = error;
            result.errorCode = unplugErrorCode;
            result.error = unplugError;
            completion = std::move(pendingRead);
        }
        if (completion) { completion(std::move(result)); }
    }
    int bootCommands() const { const QMutexLocker locker(&mutex); return boots; }
    int eraseCommands() const { const QMutexLocker locker(&mutex); return erases; }
    int closeCalls() const { const QMutexLocker locker(&mutex); return closes; }
    int programHeaderTimeouts() const { const QMutexLocker locker(&mutex); return headerTimeouts; }
    QList<qsizetype> programTransferSizes() const { const QMutexLocker locker(&mutex); return programWriteSizes; }
    bool rawMode = false;
    QList<HarmonySerialPortInfo> metadata{{42, QStringLiteral("test USB serial")}};
    std::atomic_int ioCalls = 0;
    bool zeroWrite = false;
    bool holdBaud = false;
    bool badCrc = false;
    bool unplugOnErase = false;
    bool silentSync = false;
    bool silentErase = false;
    bool delayedUsbCompletion = false;
    quint32 bootloaderVersion = 5;
    quint32 flashSize = 256;
    quint8 failCommand = 0;

private:
    void parseCommands()
    {
        while (!input.isEmpty()) {
            const quint8 command = quint8(input[0]);
            int length = command == 0x22 ? 3 : 2;
            if (command == 0x27) {
                if (input.size() < 2) {
                    if (delayedUsbCompletion && !programHeaderClock.isValid()) { programHeaderClock.start(); }
                    return;
                }
                if (programHeaderClock.isValid()) {
                    const bool expired = programHeaderClock.elapsed() > 50;
                    programHeaderClock.invalidate();
                    if (expired) {
                        ++headerTimeouts;
                        output.append(QByteArray::fromHex("1213"));
                        input.clear();
                        return;
                    }
                }
                length = quint8(input[1]) + 3;
            }
            if (input.size() < length) { return; }
            if (quint8(input[length - 1]) != 0x20) {
                output.append(QByteArray::fromHex("1213"));
                input.clear();
                return;
            }
            const QByteArray packet = input.first(length);
            input.remove(0, length);
            if (command == failCommand) {
                output.append(QByteArray::fromHex("1211"));
                continue;
            }
            if (command == 0x22) {
                const quint8 field = quint8(packet[1]);
                const quint32 value = field == 1 ? bootloaderVersion : field == 2 ? 42 : field == 4 ? flashSize : 0;
                char bytes[4];
                qToLittleEndian(value, bytes);
                output.append(bytes, 4);
            } else if (command == 0x23) {
                ++erases;
                if (silentErase) { continue; }
                if (unplugOnErase) { unplugged = true; return; }
                programmed.clear();
            } else if (command == 0x27) {
                programmed.append(packet.mid(2, quint8(packet[1])));
            } else if (command == 0x29) {
                // Independent bootloader CRC implementation, no QGC CRC helper.
                QByteArray flash = programmed;
                flash.append(QByteArray(qsizetype(flashSize) - flash.size(), char(0xff)));
                quint32 crc = 0;
                for (const char byte : flash) {
                    crc ^= quint8(byte);
                    for (int bit = 0; bit < 8; ++bit) {
                        crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0U);
                    }
                }
                char bytes[4];
                qToLittleEndian(badCrc ? crc ^ 1U : crc, bytes);
                output.append(bytes, 4);
            } else if (command == 0x30) {
                ++boots;
            }
            if (command != 0x21 || !silentSync) { output.append(QByteArray::fromHex("1210")); }
        }
    }
    mutable QMutex mutex;
    QByteArray input;
    QByteArray output;
    QByteArray programmed;
    QByteArray transmitted;
    qint32 baud = 0;
    bool automatic = false;
    int boots = 0;
    int erases = 0;
    int closes = 0;
    int headerTimeouts = 0;
    QList<qsizetype> programWriteSizes;
    QElapsedTimer programHeaderClock;
    Completion pendingRead;
    Completion pendingBaud;
    bool unplugged = false;
    int unplugErrorCode = 31400003;
    QString unplugError = QStringLiteral("USB serial device unplugged");
};

struct ProviderGuard {
    std::shared_ptr<ProtocolProvider> provider = std::make_shared<ProtocolProvider>();
    ProviderGuard() { HarmonySerialBridge::setTestProvider(provider); }
    ~ProviderGuard() { HarmonySerialBridge::setTestProvider({}); }
};

struct RunningGps {
    std::atomic_bool stop = false;
    GPSProvider gps{QStringLiteral("harmony-serial:42"), GPSProvider::GPSType::u_blox, {}, stop};
    ~RunningGps() { stop = true; (void) gps.wait(10000); }
};

struct FlashResult {
    bool opened = false;
    bool board = false;
    bool erased = false;
    bool programmed = false;
    bool verified = false;
    QString error;
};

FlashResult flash(const QString &filename, quint32 expectedFlashSize = 256)
{
    FlashResult result;
    FirmwareImage image;
    if (!image.load(filename, 42)) { result.error = QStringLiteral("Test image did not load"); return result; }
    Bootloader bootloader(false);
    QObject::connect(&bootloader, &Bootloader::updateProgress, &bootloader, [](int current, int total) {
        if (total >= 65536 && (current % 16384 == 0 || current == total)) {
            qInfo() << "Delayed USB firmware test progress" << current << total;
        }
    });
    result.opened = bootloader.open(QStringLiteral("harmony-serial:42"));
    quint32 version = 0, id = 0, size = 0;
    result.board = result.opened && bootloader.getBoardInfo(version, id, size) && version == 5 && id == 42 &&
        size == expectedFlashSize;
    result.erased = result.board && bootloader.initFlashSequence() && bootloader.erase();
    result.programmed = result.erased && bootloader.program(&image);
    result.verified = result.programmed && bootloader.verify(&image);
    result.error = bootloader.errorString();
    bootloader.close();
    return result;
}

QString makeImage(QTemporaryDir &directory)
{
    const QString filename = directory.filePath(QStringLiteral("test.bin"));
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly) || file.write(QByteArray(132, char(0xa5))) != 132) { return {}; }
    file.close();
    return filename;
}
}

void HarmonyRtkFirmwareTest::_blockingTransportReadsFragmentedBytes()
{
    ProviderGuard guard;
    guard.provider->receive(QByteArray::fromHex("d30003414243a4f833"));
    auto work = std::async(std::launch::async, []() {
        HarmonySerialIODevice port;
        port.setPortName(QStringLiteral("harmony-serial:42"));
        if (!port.open(QIODevice::ReadWrite)) { return QByteArray{}; }
        QByteArray bytes;
        while (bytes.size() < 9 && port.waitForReadyRead(500)) { bytes += port.readAll(); }
        return bytes;
    });
    QTRY_VERIFY_WITH_TIMEOUT(work.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 3000);
    QCOMPARE(work.get(), QByteArray::fromHex("d30003414243a4f833"));
}

void HarmonyRtkFirmwareTest::_firmwareControllerRetainsSerialMetadata()
{
    QVERIFY(qgcApp()->runningUnitTests());
    ProviderGuard guard;
    HarmonySerialPortInfo port{42, QStringLiteral("test USB serial")};
    port.hasUsbIdentity = true;
    port.vendorId = 9900;
    port.productId = 17;
    port.manufacturer = QStringLiteral("Test manufacturer");
    port.productName = QStringLiteral("TestBoard-BL");
    guard.provider->metadata = {port};
    {
        FirmwareUpgradeController controller;
        QSignalSpy found(&controller, &FirmwareUpgradeController::boardFound);
        controller._foundBoard(false, QGCSerialPortInfo(QStringLiteral("harmony-serial:42")),
            QGCSerialPortInfo::BoardTypePixhawk, QStringLiteral("Test board"));
        QCOMPARE(found.count(), 1);
        QCOMPARE(controller.property("boardPort").toString(), QStringLiteral("harmony-serial:42"));
        QCOMPARE(controller.property("boardDescription").toString(), port.productName);
        QCOMPARE(controller._boardInfo.vendorIdentifier(), quint16(port.vendorId));
        QCOMPARE(controller._boardInfo.productIdentifier(), quint16(port.productId));
        QVERIFY(!controller._downloadingFirmwareList);
    }
    QCOMPARE(guard.provider->ioCalls.load(), 0);
}

void HarmonyRtkFirmwareTest::_firmwareControllerBestMatch_data()
{
    QTest::addColumn<QString>("description");
    QTest::addColumn<int>("best");
    QTest::newRow("matching-bootloader") << QStringLiteral("TestBoard-BL") << 1;
    QTest::newRow("other-bootloader") << QStringLiteral("Other-BL") << 0;
    QTest::newRow("application-port") << QStringLiteral("TestBoard") << 0;
}

void HarmonyRtkFirmwareTest::_firmwareControllerBestMatch()
{
    QFETCH(QString, description);
    QFETCH(int, best);
    QVERIFY(qgcApp()->runningUnitTests());
    ProviderGuard guard;
    HarmonySerialPortInfo port{42, QStringLiteral("test USB serial")};
    port.hasUsbIdentity = true;
    port.vendorId = 9900;
    port.productId = 17;
    port.productName = description;
    guard.provider->metadata = {port};
    {
        FirmwareUpgradeController controller;
        controller._bootloaderBoardID = 42;
        const QVariant previousChibios = controller._apmChibiOSSetting->rawValue();
        const QVariant previousVehicle = controller._apmVehicleTypeSetting->rawValue();
        const auto restore = qScopeGuard([&controller, previousChibios, previousVehicle]() {
            controller._apmChibiOSSetting->setRawValue(previousChibios);
            controller._apmVehicleTypeSetting->setRawValue(previousVehicle);
        });
        controller._apmChibiOSSetting->setRawValue(0);
        controller._apmVehicleTypeSetting->setRawValue(FirmwareUpgradeController::CopterFirmware);
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
        const QStringList urls = controller.property("apmFirmwareUrls").toStringList();
        QCOMPARE(urls.size(), best ? 2 : 3);
        QCOMPARE(urls.at(best), best ? QStringLiteral("https://fixture.invalid/1.apj") : QString{});
    }
    QCOMPARE(guard.provider->ioCalls.load(), 0);
}

void HarmonyRtkFirmwareTest::_transportTimeoutAndCancellation()
{
    ProviderGuard guard;
    std::atomic_bool stop = false;
    std::atomic_bool waiting = false;
    auto work = std::async(std::launch::async, [&]() {
        HarmonySerialIODevice port(&stop);
        port.setPortName(QStringLiteral("harmony-serial:42"));
        if (!port.open(QIODevice::ReadWrite)) { return false; }
        QElapsedTimer timer;
        timer.start();
        if (port.waitForReadyRead(60) || timer.elapsed() < 40 || timer.elapsed() > 500) { return false; }
        waiting = true;
        timer.restart();
        return !port.waitForReadyRead(10000) && timer.elapsed() < 500;
    });
    QTRY_VERIFY_WITH_TIMEOUT(waiting.load() || work.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 3000);
    stop = true;
    QTRY_VERIFY_WITH_TIMEOUT(work.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 1000);
    QVERIFY(work.get());
}

void HarmonyRtkFirmwareTest::_rtkCallbacksUseNativeTransport()
{
    ProviderGuard guard;
    guard.provider->rawMode = true;
    const QByteArray correction = QByteArray::fromHex("d30003414243a4f833");
    guard.provider->receive(correction);
    struct Result { bool opened = false; bool wrote = false; bool baud = false; QByteArray read; QByteArray rtcm; };
    auto work = std::async(std::launch::async, [correction]() {
        std::atomic_bool stop = false;
        GPSProvider gps(QStringLiteral("harmony-serial:42"), GPSProvider::GPSType::u_blox, {}, stop);
        Result result;
        result.opened = gps._connectSerial();
        if (!result.opened) { return result; }
        for (int attempt = 0; result.read.size() < correction.size() && attempt < 20; ++attempt) {
            int buffer[32] = {500};
            const int size = gps.callback(GPSCallbackType::readDeviceData, buffer, sizeof(buffer));
            if (size > 0) { result.read.append(reinterpret_cast<const char *>(buffer), size); }
        }
        result.baud = gps.callback(GPSCallbackType::setBaudrate, nullptr, 115200) == 0;
        QByteArray configuration = QByteArray::fromHex("b562060000000618");
        result.wrote = gps.callback(GPSCallbackType::writeDeviceData, configuration.data(), configuration.size()) == configuration.size();
        QObject::connect(&gps, &GPSProvider::RTCMDataUpdate, &gps, [&result](const QByteArray &bytes) { result.rtcm = bytes; });
        QByteArray message = correction;
        gps.callback(GPSCallbackType::gotRTCMMessage, message.data(), message.size());
        return result;
    });
    QTRY_VERIFY_WITH_TIMEOUT(work.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 5000);
    const auto result = work.get();
    QVERIFY(result.opened);
    QCOMPARE(result.read, correction);
    QVERIFY(result.baud);
    QVERIFY(guard.provider->autoConnectRequested());
    QCOMPARE(guard.provider->configuredBaud(), 115200);
    QVERIFY(result.wrote);
    QCOMPARE(guard.provider->transmittedBytes(), QByteArray::fromHex("b562060000000618"));
    QCOMPARE(result.rtcm, correction);
}

void HarmonyRtkFirmwareTest::_writeTimeoutClosesSession()
{
    ProviderGuard guard;
    guard.provider->zeroWrite = true;
    guard.provider->rawMode = true;
    auto work = std::async(std::launch::async, []() {
        HarmonySerialIODevice port;
        port.setPortName(QStringLiteral("harmony-serial:42"));
        if (!port.open(QIODevice::ReadWrite) || port.write("pending", 7) != 7) { return false; }
        return !port.waitForBytesWritten(60) && !port.isOpen();
    });
    QTRY_VERIFY_WITH_TIMEOUT(work.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 1000);
    QVERIFY(work.get());
    QVERIFY(guard.provider->transmittedBytes().isEmpty());
}

void HarmonyRtkFirmwareTest::_baudTimeoutDiscardsLateCompletion()
{
    ProviderGuard guard;
    guard.provider->holdBaud = true;
    std::atomic_bool expired = false;
    std::promise<void> proceed;
    struct Result { bool timedOut = false; bool closed = false; bool reopened = false; };
    auto work = std::async(std::launch::async, [&]() {
        HarmonySerialIODevice port;
        port.setPortName(QStringLiteral("harmony-serial:42"));
        Result result;
        if (!port.open(QIODevice::ReadWrite)) { expired = true; return result; }
        result.timedOut = !port.setBaudRate(115200);
        result.closed = !port.isOpen();
        expired = true;
        if (proceed.get_future().wait_for(std::chrono::seconds(3)) != std::future_status::ready) { return result; }
        result.reopened = port.setBaudRate(57600) && port.open(QIODevice::ReadWrite);
        return result;
    });
    QTRY_VERIFY_WITH_TIMEOUT(expired.load(), 3000);
    guard.provider->releaseBaud();
    QTest::qWait(60);
    proceed.set_value();
    QTRY_VERIFY_WITH_TIMEOUT(work.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 3000);
    const auto result = work.get();
    QVERIFY(result.timedOut);
    QVERIFY(result.closed);
    QVERIFY(result.reopened);
    QCOMPARE(guard.provider->configuredBaud(), 57600);
}

void HarmonyRtkFirmwareTest::_rtkTerminalReadIsError()
{
    ProviderGuard guard;
    guard.provider->rawMode = true;
    auto work = std::async(std::launch::async, []() {
        std::atomic_bool stop = false;
        GPSProvider gps(QStringLiteral("harmony-serial:42"), GPSProvider::GPSType::u_blox, {}, stop);
        if (!gps._connectSerial()) { return false; }
        int buffer[32] = {60};
        if (gps.callback(GPSCallbackType::readDeviceData, buffer, sizeof(buffer)) != 0) { return false; }
        stop = true;
        buffer[0] = 10000;
        return gps.callback(GPSCallbackType::readDeviceData, buffer, sizeof(buffer)) < 0;
    });
    QTRY_VERIFY_WITH_TIMEOUT(work.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 1000);
    QVERIFY(work.get());
}

void HarmonyRtkFirmwareTest::_gpsConfigureStopsOnCancelOrUnplug_data()
{
    QTest::addColumn<bool>("unplug");
    QTest::newRow("cancel") << false;
    QTest::newRow("unplug") << true;
}

void HarmonyRtkFirmwareTest::_gpsConfigureStopsOnCancelOrUnplug()
{
    QFETCH(bool, unplug);
    ProviderGuard guard;
    guard.provider->rawMode = true;
    RunningGps running;
    running.gps.start();
    QTRY_VERIFY_WITH_TIMEOUT(!guard.provider->transmittedBytes().isEmpty(), 3000);
    if (unplug) { guard.provider->unplug(); } else { running.stop = true; }
    QElapsedTimer timer;
    timer.start();
    const bool finishedWithinBudget = running.gps.wait(500);
    const qint64 elapsed = timer.elapsed();
    QVERIFY(finishedWithinBudget);
    QVERIFY2(elapsed < 500, qPrintable(QStringLiteral("GPS configure shutdown took %1ms").arg(elapsed)));
}

void HarmonyRtkFirmwareTest::_rtkDisconnectFinishesBeforeDeletion()
{
    ProviderGuard guard;
    guard.provider->rawMode = true;
    GPSRtk rtk;
    rtk.connectGPS(QStringLiteral("harmony-serial:42"), QStringLiteral("u-blox"));
    QPointer<GPSProvider> provider = rtk.findChild<GPSProvider *>();
    QVERIFY(provider);
    // Keep test cleanup independent of the owner if a shutdown assertion fails.
    provider->setParent(nullptr);
    struct Cleanup {
        QPointer<GPSProvider> &provider;
        GPSRtk &rtk;
        ~Cleanup() {
            rtk.disconnectGPS();
            if (provider) { (void) provider->wait(10000); delete provider.data(); }
        }
    } cleanup{provider, rtk};
    QTRY_VERIFY_WITH_TIMEOUT(!guard.provider->transmittedBytes().isEmpty(), 3000);
    QElapsedTimer timer;
    timer.start();
    rtk.disconnectGPS();
    const qint64 elapsed = timer.elapsed();
    const bool finished = provider && provider->isFinished();
    if (provider && !finished) { (void) provider->wait(10000); }
    QVERIFY(finished);
    QVERIFY2(elapsed < 500, qPrintable(QStringLiteral("RTK disconnect took %1ms").arg(elapsed)));
}

void HarmonyRtkFirmwareTest::_bootloaderProgramsAndVerifies()
{
    ProviderGuard guard;
    QTemporaryDir directory(testDirectoryTemplate());
    QVERIFY(directory.isValid());
    const QString filename = makeImage(directory);
    QVERIFY(!filename.isEmpty());
    auto work = std::async(std::launch::async, [filename]() { return flash(filename); });
    QTRY_VERIFY_WITH_TIMEOUT(work.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 10000);
    const auto result = work.get();
    QVERIFY2(result.opened, qPrintable(result.error));
    QVERIFY2(result.board, qPrintable(result.error));
    QVERIFY2(result.erased, qPrintable(result.error));
    QVERIFY2(result.programmed, qPrintable(result.error));
    QVERIFY2(result.verified, qPrintable(result.error));
    QCOMPARE(guard.provider->programmedBytes(), QByteArray(132, char(0xa5)));
}

void HarmonyRtkFirmwareTest::_bootloaderProgramsWithDelayedUsbCompletion()
{
    ProviderGuard guard;
    guard.provider->delayedUsbCompletion = true;
    QByteArray payload(65540, Qt::Uninitialized);
    quint32 state = 0x93a57d2b;
    for (char &byte : payload) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        byte = char(state & 0xff);
    }
    guard.provider->flashSize = quint32(payload.size() + 256);
    QTemporaryDir directory(testDirectoryTemplate());
    QVERIFY(directory.isValid());
    const QString filename = directory.filePath(QStringLiteral("delayed-usb.bin"));
    QFile file(filename);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(payload), payload.size());
    file.close();
    auto work = std::async(std::launch::async, [filename, size = guard.provider->flashSize]() {
        return flash(filename, size);
    });
    QTRY_VERIFY_WITH_TIMEOUT(work.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 120000);
    const auto result = work.get();
    QVERIFY2(result.opened, qPrintable(result.error));
    QVERIFY2(result.board, qPrintable(result.error));
    QVERIFY2(result.erased, qPrintable(result.error));
    QVERIFY2(result.programmed, qPrintable(result.error));
    QVERIFY2(result.verified, qPrintable(result.error));
    QCOMPARE(guard.provider->programmedBytes(), payload);
    QCOMPARE(guard.provider->programHeaderTimeouts(), 0);
    const auto sizes = guard.provider->programTransferSizes();
    QCOMPARE(sizes.size(), 1025);
    for (qsizetype index = 0; index < sizes.size() - 1; ++index) { QCOMPARE(sizes[index], 67); }
    QCOMPARE(sizes.last(), 7);
    QCOMPARE(guard.provider->eraseCommands(), 1);
}

void HarmonyRtkFirmwareTest::_bootloaderRejectsCrcMismatch()
{
    ProviderGuard guard;
    guard.provider->badCrc = true;
    QTemporaryDir directory(testDirectoryTemplate());
    QVERIFY(directory.isValid());
    const QString filename = makeImage(directory);
    auto work = std::async(std::launch::async, [filename]() { return flash(filename); });
    QTRY_VERIFY_WITH_TIMEOUT(work.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 10000);
    const auto result = work.get();
    QVERIFY2(result.programmed, qPrintable(result.error));
    QVERIFY(!result.verified);
    QCOMPARE(result.error, QCoreApplication::translate("Bootloader", "CRC mismatch: board(0x%1) file(0x%2)")
        .arg(0x09f4d37e, 4, 16, QLatin1Char('0')).arg(0x09f4d37f, 4, 16, QLatin1Char('0')));
}

void HarmonyRtkFirmwareTest::_bootloaderStopsOnUnplug()
{
    ProviderGuard guard;
    guard.provider->unplugOnErase = true;
    QTemporaryDir directory(testDirectoryTemplate());
    QVERIFY(directory.isValid());
    const QString filename = makeImage(directory);
    auto work = std::async(std::launch::async, [filename]() { return flash(filename); });
    QTRY_VERIFY_WITH_TIMEOUT(work.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 3000);
    const auto result = work.get();
    QVERIFY2(result.board, qPrintable(result.error));
    QVERIFY(!result.erased);
    QVERIFY(!result.programmed);
    QVERIFY2(result.error.contains(QStringLiteral("unplugged")), qPrintable(result.error));
}

void HarmonyRtkFirmwareTest::_firmwareDisconnectDuringDownloadPreventsCommands()
{
    ProviderGuard guard;
    QTemporaryDir directory(testDirectoryTemplate());
    QVERIFY(directory.isValid());
    FirmwareImage image;
    QVERIFY(image.load(makeImage(directory), 42));
    std::atomic_bool initialized = false;
    std::atomic_bool ready = false;
    std::atomic_bool idle = false;
    PX4FirmwareUpgradeThreadController controller;
    QSignalSpy errors(&controller, &PX4FirmwareUpgradeThreadController::error);
    QSignalSpy success(&controller, &PX4FirmwareUpgradeThreadController::flashComplete);
    auto *const worker = controller._worker;
    QMetaObject::invokeMethod(worker, [worker, &controller, &initialized, &ready]() {
        worker->_bootloader = new Bootloader(false, worker, &controller._requestStop);
        quint32 version = 0, id = 0, size = 0;
        ready = worker->_bootloader->open(QStringLiteral("harmony-serial:42")) &&
            worker->_bootloader->getBoardInfo(version, id, size) && version == 5 && id == 42 && size == 256;
        initialized = true;
    }, Qt::QueuedConnection);
    QTRY_VERIFY_WITH_TIMEOUT(initialized.load(), 3000);
    QVERIFY(ready.load());
    const QByteArray beforeDisconnect = guard.provider->transmittedBytes();
    QVERIFY(beforeDisconnect.contains(QByteArray::fromHex("220120")));
    QVERIFY(beforeDisconnect.contains(QByteArray::fromHex("220220")));
    QVERIFY(beforeDisconnect.contains(QByteArray::fromHex("220420")));
    QCOMPARE(guard.provider->eraseCommands(), 0);

    // No flash request is queued while the firmware download would be pending.
    guard.provider->unplug(14400008, QStringLiteral("USB CDC transfer failed (status=5)"));
    QTRY_COMPARE_WITH_TIMEOUT(guard.provider->closeCalls(), 1, 1000);
    controller.flash(&image);
    QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), 3000);
    QMetaObject::invokeMethod(worker, [worker, &idle]() {
        idle = !worker->_bootloader;
    }, Qt::QueuedConnection);
    QTRY_VERIFY_WITH_TIMEOUT(idle.load(), 1000);
    const QString error = errors.first().first().toString();
    QVERIFY2(error.contains(QStringLiteral("14400008")), qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("status=5")), qPrintable(error));
    QCOMPARE(success.count(), 0);
    QCOMPARE(guard.provider->transmittedBytes(), beforeDisconnect);
    QCOMPARE(guard.provider->eraseCommands(), 0);
    QVERIFY(guard.provider->programmedBytes().isEmpty());
    QCOMPARE(guard.provider->bootCommands(), 0);
}

void HarmonyRtkFirmwareTest::_bootloaderRejectsUnsupportedVersion()
{
    ProviderGuard guard;
    guard.provider->bootloaderVersion = 6;
    auto work = std::async(std::launch::async, []() {
        Bootloader bootloader(false);
        if (!bootloader.open(QStringLiteral("harmony-serial:42"))) { return QString{}; }
        quint32 version = 0, id = 0, size = 0;
        if (bootloader.getBoardInfo(version, id, size)) { return QString{}; }
        return bootloader.errorString();
    });
    QTRY_VERIFY_WITH_TIMEOUT(work.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 3000);
    const QString error = work.get();
    QVERIFY2(error.contains(QCoreApplication::translate("Bootloader", "Found unsupported bootloader version: %1").arg(6)), qPrintable(error));
    QVERIFY(guard.provider->programmedBytes().isEmpty());
}

void HarmonyRtkFirmwareTest::_bootloaderTimesOutWithoutSync()
{
    ProviderGuard guard;
    guard.provider->silentSync = true;
    auto work = std::async(std::launch::async, []() {
        Bootloader bootloader(false);
        if (!bootloader.open(QStringLiteral("harmony-serial:42"))) { return QString{}; }
        quint32 version = 0, id = 0, size = 0;
        if (bootloader.getBoardInfo(version, id, size)) { return QString{}; }
        return bootloader.errorString();
    });
    QTRY_VERIFY_WITH_TIMEOUT(work.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 10000);
    const QString error = work.get();
    QVERIFY2(error.contains(QCoreApplication::translate("Bootloader", "Timeout waiting for bytes to be available")), qPrintable(error));
    QVERIFY(guard.provider->programmedBytes().isEmpty());
}

void HarmonyRtkFirmwareTest::_firmwareFailureNeverReportsSuccess_data()
{
    QTest::addColumn<int>("failureCommand");
    QTest::newRow("erase") << 0x23;
    QTest::newRow("program") << 0x27;
    QTest::newRow("verify") << 0x29;
}

void HarmonyRtkFirmwareTest::_firmwareFailureNeverReportsSuccess()
{
    QFETCH(int, failureCommand);
    ProviderGuard guard;
    guard.provider->failCommand = quint8(failureCommand);
    QTemporaryDir directory(testDirectoryTemplate());
    QVERIFY(directory.isValid());
    FirmwareImage image;
    QVERIFY(image.load(makeImage(directory), 42));
    std::atomic_bool initialized = false;
    std::atomic_bool ready = false;
    PX4FirmwareUpgradeThreadController controller;
    QSignalSpy errors(&controller, &PX4FirmwareUpgradeThreadController::error);
    QSignalSpy success(&controller, &PX4FirmwareUpgradeThreadController::flashComplete);
    auto *const worker = controller._worker;
    QMetaObject::invokeMethod(worker, [worker, &controller, &initialized, &ready]() {
        worker->_bootloader = new Bootloader(false, worker, &controller._requestStop);
        quint32 version = 0, id = 0, size = 0;
        ready = worker->_bootloader->open(QStringLiteral("harmony-serial:42")) &&
            worker->_bootloader->getBoardInfo(version, id, size);
        initialized = true;
    }, Qt::QueuedConnection);
    QTRY_VERIFY_WITH_TIMEOUT(initialized.load(), 3000);
    QVERIFY(ready.load());
    controller.flash(&image);
    QTRY_VERIFY_WITH_TIMEOUT(errors.count() > 0, 5000);
    QTest::qWait(1300);
    QCOMPARE(success.count(), 0);
    QCOMPARE(guard.provider->bootCommands(), 0);
}

void HarmonyRtkFirmwareTest::_firmwareCancelStopsDiscovery()
{
    ProviderGuard guard;
    std::atomic_bool searching = false;
    std::atomic_bool stopped = false;
    PX4FirmwareUpgradeThreadController controller;
    controller.startFindBoardLoop();
    auto *const worker = controller._worker;
    QMetaObject::invokeMethod(worker, [worker, &searching]() {
        searching = worker->_findBoardTimer && worker->_findBoardTimer->isActive();
    }, Qt::QueuedConnection);
    QTRY_VERIFY_WITH_TIMEOUT(searching.load(), 1000);
    controller.cancel();
    QMetaObject::invokeMethod(worker, [worker, &stopped]() {
        stopped = worker->_findBoardTimer && !worker->_findBoardTimer->isActive();
    }, Qt::QueuedConnection);
    QTRY_VERIFY_WITH_TIMEOUT(stopped.load(), 1000);
}

void HarmonyRtkFirmwareTest::_firmwareSuccessReportsOnce()
{
    ProviderGuard guard;
    QTemporaryDir directory(testDirectoryTemplate());
    QVERIFY(directory.isValid());
    FirmwareImage image;
    QVERIFY(image.load(makeImage(directory), 42));
    std::atomic_bool initialized = false;
    std::atomic_bool ready = false;
    PX4FirmwareUpgradeThreadController controller;
    QSignalSpy errors(&controller, &PX4FirmwareUpgradeThreadController::error);
    QSignalSpy success(&controller, &PX4FirmwareUpgradeThreadController::flashComplete);
    auto *const worker = controller._worker;
    QMetaObject::invokeMethod(worker, [worker, &controller, &initialized, &ready]() {
        worker->_bootloader = new Bootloader(false, worker, &controller._requestStop);
        quint32 version = 0, id = 0, size = 0;
        ready = worker->_bootloader->open(QStringLiteral("harmony-serial:42")) &&
            worker->_bootloader->getBoardInfo(version, id, size);
        initialized = true;
    }, Qt::QueuedConnection);
    QTRY_VERIFY_WITH_TIMEOUT(initialized.load(), 3000);
    QVERIFY(ready.load());
    controller.flash(&image);
    QTRY_COMPARE_WITH_TIMEOUT(success.count(), 1, 5000);
    QCOMPARE(errors.count(), 0);
    QCOMPARE(guard.provider->programmedBytes(), QByteArray(132, char(0xa5)));
    QTest::qWait(100);
    QCOMPARE(success.count(), 1);
}

void HarmonyRtkFirmwareTest::_firmwareCancelDuringErase()
{
    ProviderGuard guard;
    guard.provider->silentErase = true;
    QTemporaryDir directory(testDirectoryTemplate());
    QVERIFY(directory.isValid());
    FirmwareImage image;
    QVERIFY(image.load(makeImage(directory), 42));
    std::atomic_bool initialized = false;
    std::atomic_bool ready = false;
    std::atomic_bool idle = false;
    PX4FirmwareUpgradeThreadController controller;
    QSignalSpy success(&controller, &PX4FirmwareUpgradeThreadController::flashComplete);
    auto *const worker = controller._worker;
    QMetaObject::invokeMethod(worker, [worker, &controller, &initialized, &ready]() {
        worker->_bootloader = new Bootloader(false, worker, &controller._requestStop);
        quint32 version = 0, id = 0, size = 0;
        ready = worker->_bootloader->open(QStringLiteral("harmony-serial:42")) &&
            worker->_bootloader->getBoardInfo(version, id, size);
        initialized = true;
    }, Qt::QueuedConnection);
    QTRY_VERIFY_WITH_TIMEOUT(initialized.load(), 3000);
    QVERIFY(ready.load());
    controller.flash(&image);
    QTRY_COMPARE_WITH_TIMEOUT(guard.provider->eraseCommands(), 1, 1000);
    QElapsedTimer timer;
    timer.start();
    controller.cancel();
    QMetaObject::invokeMethod(worker, [worker, &idle]() {
        idle = !worker->_bootloader && !worker->_findBoardTimer->isActive();
    }, Qt::QueuedConnection);
    QTRY_VERIFY_WITH_TIMEOUT(idle.load(), 2000);
    QVERIFY(timer.elapsed() < 2000);
    QCOMPARE(success.count(), 0);
    QCOMPARE(guard.provider->bootCommands(), 0);
    QVERIFY(guard.provider->programmedBytes().isEmpty());
}

void HarmonyRtkFirmwareTest::_firmwareCancelAndRestart_data()
{
    QTest::addColumn<bool>("cancelAgain");
    QTest::newRow("restart") << false;
    QTest::newRow("restart-then-cancel") << true;
}

void HarmonyRtkFirmwareTest::_firmwareCancelAndRestart()
{
    QFETCH(bool, cancelAgain);
    ProviderGuard guard;
    guard.provider->silentErase = true;
    QTemporaryDir directory(testDirectoryTemplate());
    QVERIFY(directory.isValid());
    FirmwareImage image;
    QVERIFY(image.load(makeImage(directory), 42));
    std::atomic_bool initialized = false;
    std::atomic_bool ready = false;
    std::atomic_bool observed = false;
    std::atomic_bool searching = false;
    std::atomic_bool stopped = false;
    PX4FirmwareUpgradeThreadController controller;
    QSignalSpy success(&controller, &PX4FirmwareUpgradeThreadController::flashComplete);
    auto *const worker = controller._worker;
    QMetaObject::invokeMethod(worker, [worker, &controller, &initialized, &ready]() {
        worker->_bootloader = new Bootloader(false, worker, &controller._requestStop);
        quint32 version = 0, id = 0, size = 0;
        ready = worker->_bootloader->open(QStringLiteral("harmony-serial:42")) &&
            worker->_bootloader->getBoardInfo(version, id, size);
        initialized = true;
    }, Qt::QueuedConnection);
    QTRY_VERIFY_WITH_TIMEOUT(initialized.load(), 3000);
    QVERIFY(ready.load());
    controller.flash(&image);
    QTRY_COMPARE_WITH_TIMEOUT(guard.provider->eraseCommands(), 1, 1000);
    controller.cancel();
    controller.startFindBoardLoop();
    if (cancelAgain) { controller.cancel(); }
    guard.provider->receive(QByteArray::fromHex("1210"));
    QMetaObject::invokeMethod(worker, [worker, &controller, &observed, &searching, &stopped]() {
        searching = worker->_findBoardTimer->isActive();
        stopped = controller._requestStop.load();
        observed = true;
    }, Qt::QueuedConnection);
    QTRY_VERIFY_WITH_TIMEOUT(observed.load(), 5000);
    QCOMPARE(success.count(), 0);
    QCOMPARE(guard.provider->bootCommands(), 0);
    QVERIFY(guard.provider->programmedBytes().isEmpty());
    QCOMPARE(searching.load(), !cancelAgain);
    QCOMPARE(stopped.load(), cancelAgain);
}

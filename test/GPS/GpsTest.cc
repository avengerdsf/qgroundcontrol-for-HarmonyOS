/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "GpsTest.h"
#include "LinkManager.h"
#include "MultiVehicleManager.h"
#include "QmlObjectListModel.h"
#ifndef QGC_NO_SERIAL_LINK
#include "RTCMMavlink.h"
#endif

#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

void GpsTest::cleanup()
{
    LinkManager::instance()->disconnectAll();
    QTRY_COMPARE_WITH_TIMEOUT(MultiVehicleManager::instance()->vehicles()->count(), 0, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(MultiVehicleManager::instance()->activeVehicle() == nullptr, 3000);
    _mockLink = nullptr;
    _vehicle = nullptr;
    UnitTest::cleanup();
}

void GpsTest::_testGpsRTCM()
{
#ifdef QGC_NO_SERIAL_LINK
    QSKIP("GPS RTCM test requires serial link support");
#else
    _connectMockLinkNoInitialConnectSequence();
    QVERIFY(_mockLink);
    QVERIFY(_vehicle);
    QSignalSpy written(_mockLink, &MockLink::writeBytesQueuedSignal);
    QVERIFY(written.isValid());
    MockLink *const secondLink = MockLink::startNoInitialConnectMockLink(false);
    QVERIFY(secondLink);
    QTRY_COMPARE_WITH_TIMEOUT(MultiVehicleManager::instance()->vehicles()->count(), 2, 3000);
    QSignalSpy secondWritten(secondLink, &MockLink::writeBytesQueuedSignal);
    QVERIFY(secondWritten.isValid());

    RTCMMavlink rtcm;
    const QByteArray shortMessage = QByteArray::fromHex("d30003414243a4f833");
    const QByteArray twoFragments(240, char(0x42));
    const QByteArray fourFragments(719, char(0x7b));
    rtcm.RTCMDataUpdate(shortMessage);
    rtcm.RTCMDataUpdate(twoFragments);
    rtcm.RTCMDataUpdate(fourFragments);

    const auto messages = [](const QSignalSpy &spy) {
        QList<mavlink_gps_rtcm_data_t> result;
        mavlink_message_t receiveBuffer{}, message{};
        mavlink_status_t parseStatus{}, messageStatus{};
        for (const auto &arguments : spy) {
            for (const unsigned char byte : arguments.first().toByteArray()) {
                if (mavlink_frame_char_buffer(&receiveBuffer, &parseStatus, byte, &message, &messageStatus) == MAVLINK_FRAMING_OK &&
                        message.msgid == MAVLINK_MSG_ID_GPS_RTCM_DATA) {
                    mavlink_gps_rtcm_data_t data{};
                    mavlink_msg_gps_rtcm_data_decode(&message, &data);
                    result.append(data);
                }
            }
        }
        return result;
    };
    QTRY_COMPARE_WITH_TIMEOUT(messages(written).size(), 7, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(messages(secondWritten).size(), 7, 3000);
    const auto packets = messages(written);
    const auto secondPackets = messages(secondWritten);
    for (int i = 0; i < packets.size(); ++i) {
        QCOMPARE(secondPackets[i].flags, packets[i].flags);
        QCOMPARE(secondPackets[i].len, packets[i].len);
        QCOMPARE(QByteArray(reinterpret_cast<const char *>(secondPackets[i].data), secondPackets[i].len),
            QByteArray(reinterpret_cast<const char *>(packets[i].data), packets[i].len));
    }
    QCOMPARE(packets[0].flags, uint8_t(0));
    QCOMPARE(packets[0].len, uint8_t(9));
    QCOMPARE(QByteArray(reinterpret_cast<const char *>(packets[0].data), packets[0].len), shortMessage);

    const QList<uint8_t> expectedFlags{9, 11, 17, 19, 21, 23};
    const QList<uint8_t> expectedLengths{180, 60, 180, 180, 180, 179};
    QByteArray reconstructedTwo, reconstructedFour;
    for (int i = 1; i < packets.size(); ++i) {
        QCOMPARE(packets[i].flags, expectedFlags[i - 1]);
        QCOMPARE(packets[i].len, expectedLengths[i - 1]);
        QByteArray &destination = i < 3 ? reconstructedTwo : reconstructedFour;
        destination.append(reinterpret_cast<const char *>(packets[i].data), packets[i].len);
    }
    QCOMPARE(reconstructedTwo, twoFragments);
    QCOMPARE(reconstructedFour, fourFragments);

    for (int i = 0; i < 31; ++i) { rtcm.RTCMDataUpdate(shortMessage); }
    QTRY_COMPARE_WITH_TIMEOUT(messages(written).size(), 38, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(messages(secondWritten).size(), 38, 3000);
    QCOMPARE(messages(written)[35].flags, uint8_t(248));
    QCOMPARE(messages(written)[36].flags, uint8_t(0));
    QCOMPARE(messages(written)[37].flags, uint8_t(8));
    QCOMPARE(messages(secondWritten)[36].flags, uint8_t(0));
#endif
}

void GpsTest::_testGpsRTCMFrameBoundaries_data()
{
    QTest::addColumn<int>("size");
    QTest::addColumn<int>("initialSequence");
    QTest::addColumn<QList<int>>("flags");
    QTest::addColumn<QList<int>>("lengths");
    QTest::newRow("empty") << 0 << 0 << QList<int>{} << QList<int>{};
    QTest::newRow("180") << 180 << 0 << QList<int>{0} << QList<int>{180};
    QTest::newRow("360") << 360 << 0 << QList<int>{1, 3, 5} << QList<int>{180, 180, 0};
    QTest::newRow("540") << 540 << 0 << QList<int>{1, 3, 5, 7} << QList<int>{180, 180, 180, 0};
    QTest::newRow("720") << 720 << 0 << QList<int>{1, 3, 5, 7} << QList<int>{180, 180, 180, 180};
    QTest::newRow("1029") << 1029 << 0 << QList<int>{1, 3, 5, 7, 9, 11} << QList<int>{180, 180, 180, 180, 180, 129};
    QTest::newRow("1029-seq1") << 1029 << 1 << QList<int>{9, 11, 13, 15, 17, 19} << QList<int>{180, 180, 180, 180, 180, 129};
    QTest::newRow("1029-seq31") << 1029 << 31 << QList<int>{249, 251, 253, 255, 1, 3} << QList<int>{180, 180, 180, 180, 180, 129};
}

void GpsTest::_testGpsRTCMFrameBoundaries()
{
#ifdef QGC_NO_SERIAL_LINK
    QSKIP("GPS RTCM test requires serial link support");
#else
    QFETCH(int, size);
    QFETCH(int, initialSequence);
    QFETCH(QList<int>, flags);
    QFETCH(QList<int>, lengths);
    _connectMockLinkNoInitialConnectSequence();
    QVERIFY(_mockLink);
    QSignalSpy written(_mockLink, &MockLink::writeBytesQueuedSignal);
    QVERIFY(written.isValid());
    QByteArray bytes(size, char(0x7b));
    if (size == 1029) {
        bytes = QByteArray::fromHex("d303ff") + QByteArray(1023, char(0x7b)) + QByteArray::fromHex("b7a7e1");
    }
    RTCMMavlink rtcm;
    const auto messages = [&written]() {
        QList<mavlink_gps_rtcm_data_t> result;
        mavlink_message_t receiveBuffer{}, message{};
        mavlink_status_t parseStatus{}, messageStatus{};
        for (const auto &arguments : written) {
            for (const unsigned char byte : arguments.first().toByteArray()) {
                if (mavlink_frame_char_buffer(&receiveBuffer, &parseStatus, byte, &message, &messageStatus) == MAVLINK_FRAMING_OK &&
                        message.msgid == MAVLINK_MSG_ID_GPS_RTCM_DATA) {
                    mavlink_gps_rtcm_data_t data{};
                    mavlink_msg_gps_rtcm_data_decode(&message, &data);
                    result.append(data);
                }
            }
        }
        return result;
    };
    const QByteArray shortMessage = QByteArray::fromHex("d30003414243a4f833");
    for (int i = 0; i < initialSequence; ++i) { rtcm.RTCMDataUpdate(shortMessage); }
    QTRY_COMPARE_WITH_TIMEOUT(messages().size(), initialSequence, 3000);
    written.clear();
    rtcm.RTCMDataUpdate(bytes);
    QTest::qWait(200);
    QTRY_COMPARE_WITH_TIMEOUT(messages().size(), flags.size(), 3000);
    const auto packets = messages();
    QList<QByteArray> completed;
    QByteArray fragments[4];
    int seen = 0;
    int previousSequence = -1;
    for (int i = 0; i < packets.size(); ++i) {
        const auto &packet = packets[i];
        QCOMPARE(int(packet.flags), flags[i]);
        QCOMPARE(int(packet.len), lengths[i]);
        const QByteArray payload(reinterpret_cast<const char *>(packet.data), packet.len);
        if (!(packet.flags & 1)) {
            completed.append(payload);
            continue;
        }
        const int sequence = packet.flags >> 3;
        if (sequence != previousSequence) {
            seen = 0;
            for (auto &fragment : fragments) { fragment.clear(); }
            previousSequence = sequence;
        }
        const int fragment = (packet.flags >> 1) & 3;
        fragments[fragment] = payload;
        seen |= 1 << fragment;
        const int expectedMask = packet.len < 180 ? (1 << (fragment + 1)) - 1 : 15;
        if ((seen & expectedMask) == expectedMask) {
            QByteArray bundle;
            for (int f = 0; f <= fragment; ++f) { bundle.append(fragments[f]); }
            completed.append(bundle);
            seen = 0;
        }
    }
    QByteArray reconstructed;
    for (const auto &bundle : completed) { reconstructed.append(bundle); }
    QCOMPARE(reconstructed, bytes);
    QCOMPARE(completed.size(), size == 0 ? 0 : size > 720 ? 2 : 1);
    if (size == 1029) {
        rtcm.RTCMDataUpdate(shortMessage);
        QTRY_COMPARE_WITH_TIMEOUT(messages().size(), flags.size() + 1, 3000);
        const auto next = messages().last();
        const int nextFlags = initialSequence == 0 ? 16 : initialSequence == 1 ? 24 : 8;
        QCOMPARE(int(next.flags), nextFlags);
        QCOMPARE(QByteArray(reinterpret_cast<const char *>(next.data), next.len), shortMessage);
    }
#endif
}

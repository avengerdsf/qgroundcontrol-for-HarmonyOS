/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "VehicleLinkManagerTest.h"
#include "MockLink.h"
#include "MockLinkWorker.h"
#include "LinkManager.h"
#include "MultiVehicleManager.h"
#include "Vehicle.h"
#include "ParameterManager.h"
#include "MultiSignalSpyV2.h"

#include <QtTest/QTest>
#include <QtTest/QSignalSpy>
#include <QtCore/QThread>
#include <QtCore/QScopeGuard>

void VehicleLinkManagerTest::init()
{
    UnitTest::init();

    QCOMPARE(LinkManager::instance()->links().count(), 0);
    QCOMPARE(MultiVehicleManager::instance()->vehicles()->count(), 0);
}

void VehicleLinkManagerTest::cleanup()
{
    // Disconnect all links
    const bool hadActiveVehicle = MultiVehicleManager::instance()->activeVehicle() != nullptr;
    QSignalSpy spyActiveVehicleChanged(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged);
    LinkManager::instance()->disconnectAll();
    QTRY_VERIFY_WITH_TIMEOUT(LinkManager::instance()->links().count() == 0 &&
        MultiVehicleManager::instance()->vehicles()->count() == 0 &&
        MultiVehicleManager::instance()->activeVehicle() == nullptr, 1000);
    if (hadActiveVehicle) {
        QVERIFY(!spyActiveVehicleChanged.isEmpty());
    }

    UnitTest::cleanup();
}

void VehicleLinkManagerTest::_highLatencyInitialFrameTest()
{
    SharedLinkConfigurationPtr mockConfig = std::make_shared<MockConfiguration>(QStringLiteral("High latency initial frame"));
    qobject_cast<MockConfiguration*>(mockConfig.get())->setHighLatency(true);
    MockLink mockLink(mockConfig);
    const auto releaseChannels = qScopeGuard([&mockLink]() {
        mockLink.disconnect();
        mockLink._freeMavlinkChannel();
    });
    QVERIFY(mockLink._allocateMavlinkChannel());

    // Let an already running worker finish its initial tasks before connecting.
    if (mockLink._workerThread->isRunning()) {
        QVERIFY(QMetaObject::invokeMethod(mockLink._worker, []() {}, Qt::BlockingQueuedConnection));
    }

    QSignalSpy spyBytesReceived(&mockLink, &LinkInterface::bytesReceived);
    QVERIFY(spyBytesReceived.isValid());
    QVERIFY(mockLink._connect());
    QVERIFY(!spyBytesReceived.isEmpty() || spyBytesReceived.wait(500));

    mavlink_message_t parseMessage{};
    mavlink_status_t parseStatus{};
    mavlink_message_t receivedMessage{};
    mavlink_status_t receivedStatus{};
    bool receivedFrame = false;
    for (const char byte : spyBytesReceived.takeFirst()[1].toByteArray()) {
        if (mavlink_frame_char_buffer(&parseMessage, &parseStatus, static_cast<uint8_t>(byte),
            &receivedMessage, &receivedStatus) == MAVLINK_FRAMING_OK) {
            receivedFrame = true;
        }
    }
    QVERIFY(receivedFrame);
    QCOMPARE(receivedMessage.msgid, static_cast<uint32_t>(MAVLINK_MSG_ID_HIGH_LATENCY2));
    QCOMPARE(receivedMessage.sysid, static_cast<uint8_t>(mockLink.vehicleId()));
}

void VehicleLinkManagerTest::_disconnectDuringInitialConnect()
{
    for (int i = 0; i < 5; ++i) {
        SharedLinkConfigurationPtr mockConfig;
        SharedLinkInterfacePtr mockLink;
        QSignalSpy spyVehicleCreate(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged);
        _startMockLink(i, false, true, mockConfig, mockLink);
        QVERIFY(mockLink);
        QVERIFY(spyVehicleCreate.wait(1000));
        Vehicle *const vehicle = MultiVehicleManager::instance()->activeVehicle();
        QVERIFY(vehicle);
        QTRY_VERIFY_WITH_TIMEOUT(vehicle->parameterManager()->loadProgress() > 0.0 &&
            vehicle->parameterManager()->loadProgress() < 1.0, 5000);
        QVERIFY(!vehicle->parameterManager()->parametersReady());
        QVERIFY(!vehicle->isInitialConnectComplete());

        mavlink_message_t requestMessage{};
        char paramId[MAVLINK_MSG_PARAM_REQUEST_READ_FIELD_PARAM_ID_LEN]{};
        (void) mavlink_msg_param_request_read_pack(255, MAV_COMP_ID_MISSIONPLANNER, &requestMessage,
            static_cast<uint8_t>(vehicle->id()), MAV_COMP_ID_AUTOPILOT1, paramId, 0);
        uint8_t requestBuffer[MAVLINK_MAX_PACKET_LEN]{};
        const uint16_t requestLength = mavlink_msg_to_send_buffer(requestBuffer, &requestMessage);
        const QByteArray requestBytes(reinterpret_cast<const char*>(requestBuffer), requestLength);

        QThread *const workerThread = mockLink->findChild<QThread*>();
        QVERIFY(workerThread);
        QVERIFY(workerThread->isRunning());
        bool workerStoppedAtDisconnect = false;
        (void) connect(mockLink.get(), &LinkInterface::disconnected, this, [&workerStoppedAtDisconnect, workerThread]() {
            workerStoppedAtDisconnect = !workerThread->isRunning();
        });
        QSignalSpy spyVehicleDelete(vehicle, &QObject::destroyed);
        QSignalSpy spyBytesReceived(mockLink.get(), &LinkInterface::bytesReceived);
        mockLink->disconnect();
        QVERIFY(workerStoppedAtDisconnect);
        QVERIFY(spyVehicleDelete.wait(1000));
        QCOMPARE(LinkManager::instance()->links().count(), 0);
        QCOMPARE(MultiVehicleManager::instance()->vehicles()->count(), 0);

        spyBytesReceived.clear();
        mockLink->writeBytesThreadSafe(requestBytes.constData(), requestBytes.size());
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        QCOMPARE(spyBytesReceived.count(), 0);
        mockLink->disconnect();
    }
}

void VehicleLinkManagerTest::_vehicleSystemIdWrapTest()
{
    const int previousNextId = MockLink::_nextVehicleSystemId;
    const auto restoreNextId = qScopeGuard([previousNextId]() {
        MockLink::_nextVehicleSystemId = previousNextId;
    });
    MockLink::_nextVehicleSystemId = 254;

    const auto vehicleId = [](bool increment) {
        SharedLinkConfigurationPtr config = std::make_shared<MockConfiguration>(QStringLiteral("System ID boundary"));
        qobject_cast<MockConfiguration*>(config.get())->setIncrementVehicleId(increment);
        MockLink link(config);
        return link.vehicleId();
    };

    QCOMPARE(vehicleId(true), 254);
    QCOMPARE(vehicleId(true), 255);
    QCOMPARE(vehicleId(false), 1);
    QCOMPARE(vehicleId(false), 1);
    QCOMPARE(vehicleId(true), 1);
    QCOMPARE(vehicleId(false), 2);
    QCOMPARE(MockLink::_nextVehicleSystemId, 2);
}

void VehicleLinkManagerTest::_simpleLinkTest()
{
    SharedLinkConfigurationPtr mockConfig;
    SharedLinkInterfacePtr mockLink;

    QSignalSpy spyVehicleCreate(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged);

    _startMockLink(1, false /*highLatency*/, true /*incrementVehicleId*/, mockConfig, mockLink);
    QVERIFY(mockConfig);
    QVERIFY(mockLink);

    const QSignalSpy spyConfigDelete(mockConfig.get(), &QObject::destroyed);
    const QSignalSpy spyLinkDelete(mockLink.get(), &QObject::destroyed);
    QVERIFY(spyConfigDelete.isValid());
    QVERIFY(spyLinkDelete.isValid());

    QCOMPARE(spyVehicleCreate.wait(1000), true);
    QCOMPARE(MultiVehicleManager::instance()->vehicles()->count(), 1);
    Vehicle *const vehicle = MultiVehicleManager::instance()->activeVehicle();
    QVERIFY(vehicle);
    QSignalSpy spyVehicleDelete(vehicle, &QObject::destroyed);
    QSignalSpy spyVehicleInitialConnectComplete(vehicle, &Vehicle::initialConnectComplete);

    QCOMPARE(mockConfig.use_count(), 2); // Refs: This method, MockLink
    QCOMPARE(mockLink.use_count(), 3); // Refs: This method, LinkManager, Vehicle

    // We wait for the full initial connect sequence to complete to catch anby ComponentInformationManager bugs
    QCOMPARE(spyVehicleInitialConnectComplete.wait(30000), true);

    mockLink->disconnect();

    // Vehicle should go away due to disconnect
    QCOMPARE(spyVehicleDelete.wait(500), true);

    // Config/Link should still be alive due to the last refs being held by this method
    QCOMPARE(spyConfigDelete.count(), 0);
    QCOMPARE(spyLinkDelete.count(), 0);
    QCOMPARE(mockConfig.use_count(), 2); // Refs: This method, MockLink
    QCOMPARE(mockLink.use_count(), 1); // Refs: This method

    // Let go of our refs from this method and config and link should go away
    mockConfig.reset();
    mockLink.reset();
    QCOMPARE(mockConfig.use_count(), 0);
    QCOMPARE(mockLink.use_count(), 0);
    QCOMPARE(spyLinkDelete.count(), 1);
    QCOMPARE(spyConfigDelete.count(), 1);
}

void VehicleLinkManagerTest::_simpleCommLossTest()
{
    SharedLinkConfigurationPtr mockConfig;
    SharedLinkInterfacePtr mockLink;

    QSignalSpy spyVehicleCreate(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged);

    _startMockLink(1, false /*highLatency*/, true /*incrementVehicleId*/, mockConfig, mockLink);
    MockLink *const pMockLink = qobject_cast<MockLink*>(mockLink.get());

    QCOMPARE(spyVehicleCreate.wait(1000), true);
    QCOMPARE(MultiVehicleManager::instance()->vehicles()->count(), 1);
    Vehicle* vehicle = MultiVehicleManager::instance()->activeVehicle();
    QVERIFY(vehicle);
    QSignalSpy spyVehicleInitialConnectComplete(vehicle, &Vehicle::initialConnectComplete);
    QCOMPARE(spyVehicleInitialConnectComplete.wait(30000), true);

    QSignalSpy spyCommLostChanged(vehicle->vehicleLinkManager(), &VehicleLinkManager::communicationLostChanged);
    pMockLink->setCommLost(true);
    QCOMPARE(spyCommLostChanged.wait(VehicleLinkManager::_heartbeatMaxElpasedMSecs * 2), true);
    QCOMPARE(spyCommLostChanged.count(), 1);
    QCOMPARE(spyCommLostChanged[0][0].toBool(), true);

    spyCommLostChanged.clear();
    pMockLink->setCommLost(false);
    QCOMPARE(spyCommLostChanged.wait(VehicleLinkManager::_heartbeatMaxElpasedMSecs * 2), true);
    QCOMPARE(spyCommLostChanged.count(), 1);
    QCOMPARE(spyCommLostChanged[0][0].toBool(), false);

    spyCommLostChanged.clear();
    vehicle->vehicleLinkManager()->setCommunicationLostEnabled(false);
    pMockLink->setCommLost(true);
    QCOMPARE(spyCommLostChanged.wait(VehicleLinkManager::_heartbeatMaxElpasedMSecs * 2), false);

    spyCommLostChanged.clear();
    vehicle->vehicleLinkManager()->setCommunicationLostEnabled(true);
    QCOMPARE(spyCommLostChanged.wait(VehicleLinkManager::_heartbeatMaxElpasedMSecs * 2), true);
    QCOMPARE(spyCommLostChanged.count(), 1);
}

void VehicleLinkManagerTest::_multiLinkSingleVehicleTest()
{
    SharedLinkConfigurationPtr mockConfig1;
    SharedLinkInterfacePtr mockLink1;
    SharedLinkConfigurationPtr mockConfig2;
    SharedLinkInterfacePtr mockLink2;

    QSignalSpy spyVehicleCreate(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged);

    _startMockLink(1, false /*highLatency*/, false /*incrementVehicleId*/, mockConfig1, mockLink1);
    _startMockLink(2, false /*highLatency*/, false /*incrementVehicleId*/, mockConfig2, mockLink2);

    QCOMPARE(spyVehicleCreate.wait(1000), true);
    QCOMPARE(MultiVehicleManager::instance()->vehicles()->count(), 1);
    Vehicle *const vehicle = MultiVehicleManager::instance()->activeVehicle();
    VehicleLinkManager *const vehicleLinkManager = vehicle->vehicleLinkManager();
    QVERIFY(vehicle);
    QVERIFY(vehicleLinkManager);
    QSignalSpy spyVehicleInitialConnectComplete(vehicle, &Vehicle::initialConnectComplete);
    QCOMPARE(spyVehicleInitialConnectComplete.wait(30000), true);

    // The first link to start sending a heartbeat will be the primary link.
    // Depending on how the thread scheduling works, that could be the mockLink2.
    const SharedLinkInterfacePtr primaryLink = vehicleLinkManager->primaryLink().lock();
    QVERIFY(primaryLink == mockLink1 || primaryLink == mockLink2);
    MockLink *pMockLink1 = qobject_cast<MockLink*>(mockLink1.get());
    MockLink *pMockLink2 = qobject_cast<MockLink*>(mockLink2.get());
    if (primaryLink == mockLink2) {
        std::swap(pMockLink1, pMockLink2);
    }

    const QStringList rgNames = vehicleLinkManager->linkNames();
    QStringList rgStatus = vehicleLinkManager->linkStatuses();
    QCOMPARE(rgNames.count(), 2);
    QCOMPARE(rgNames[0], mockConfig1->name());
    QCOMPARE(rgNames[1], mockConfig2->name());
    QCOMPARE(rgStatus.count(), 2);
    QVERIFY(rgStatus[0].isEmpty());
    QVERIFY(rgStatus[1].isEmpty());

    MultiSignalSpyV2 multiSpy;
    QVERIFY(multiSpy.init(vehicleLinkManager));

    // Comm lost on 2: 1 is primary, 2 is secondary so comm loss/regain on 2 should only update status text

    pMockLink2->setCommLost(true);
    QCOMPARE(multiSpy.waitForSignal (_linkStatusesChangedSignalName, VehicleLinkManager::_heartbeatMaxElpasedMSecs * 2), true);
    QVERIFY(multiSpy.checkOnlySignalByMask(multiSpy.signalNameToMask(_linkStatusesChangedSignalName)));

    rgStatus = vehicleLinkManager->linkStatuses();
    QCOMPARE(rgStatus.count(), 2);
    QVERIFY(rgStatus[0].isEmpty());
    QVERIFY(!rgStatus[1].isEmpty());

    multiSpy.clearAllSignals();

    pMockLink2->setCommLost(false);
    QCOMPARE(multiSpy.waitForSignal (_linkStatusesChangedSignalName, VehicleLinkManager::_heartbeatMaxElpasedMSecs * 2), true);
    QVERIFY(multiSpy.checkOnlySignalByMask(multiSpy.signalNameToMask(_linkStatusesChangedSignalName)));

    rgStatus = vehicleLinkManager->linkStatuses();
    QCOMPARE(rgStatus.count(), 2);
    QVERIFY(rgStatus[0].isEmpty());
    QVERIFY(rgStatus[1].isEmpty());

    multiSpy.clearAllSignals();

    // Comm loss on 1: 1 is primary so should trigger switch of primary to 2

    pMockLink1->setCommLost(true);
    QCOMPARE(multiSpy.waitForSignal (_primaryLinkChangedSignalName, VehicleLinkManager::_heartbeatMaxElpasedMSecs * 2), true);
    const quint32 signalMask = multiSpy.signalNameToMask(_primaryLinkChangedSignalName) | multiSpy.signalNameToMask(_linkStatusesChangedSignalName);
    QVERIFY(multiSpy.checkOnlySignalByMask(signalMask));
    QCOMPARE(pMockLink2,vehicleLinkManager->primaryLink().lock().get());

    rgStatus = vehicleLinkManager->linkStatuses();
    QCOMPARE(rgStatus.count(), 2);
    QVERIFY(!rgStatus[0].isEmpty());
    QVERIFY(rgStatus[1].isEmpty());

    multiSpy.clearAllSignals();

    // Comm regained on 1 should leave 2 as primary and only update status

    pMockLink1->setCommLost(false);
    QCOMPARE(multiSpy.waitForSignal (_linkStatusesChangedSignalName, VehicleLinkManager::_heartbeatMaxElpasedMSecs * 2),    true);
    QVERIFY(multiSpy.checkOnlySignalByMask(multiSpy.signalNameToMask(_linkStatusesChangedSignalName)));
    QCOMPARE(pMockLink2, vehicleLinkManager->primaryLink().lock().get());

    rgStatus = vehicleLinkManager->linkStatuses();
    QCOMPARE(rgStatus.count(), 2);
    QVERIFY(rgStatus[0].isEmpty());
    QVERIFY(rgStatus[1].isEmpty());

    multiSpy.clearAllSignals();
}

void VehicleLinkManagerTest::_connectionRemovedTest()
{
    SharedLinkConfigurationPtr mockConfig;
    SharedLinkInterfacePtr mockLink;

    QSignalSpy spyVehicleCreate(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged);

    _startMockLink(1, false /*highLatency*/, true /*incrementVehicleId*/, mockConfig, mockLink);
    MockLink *const pMockLink = qobject_cast<MockLink*>(mockLink.get());

    QCOMPARE(spyVehicleCreate.wait(1000), true);
    Vehicle *const vehicle = MultiVehicleManager::instance()->activeVehicle();
    QVERIFY(vehicle);
    QSignalSpy spyVehicleInitialConnectComplete(vehicle, &Vehicle::initialConnectComplete);
    QCOMPARE(spyVehicleInitialConnectComplete.wait(30000), true);

    QSignalSpy spyCommLostChanged(vehicle->vehicleLinkManager(), &VehicleLinkManager::communicationLostChanged);

    // Connection removed should just signal communication lost

    pMockLink->simulateConnectionRemoved();
    QCOMPARE(spyCommLostChanged.wait(VehicleLinkManager::_heartbeatMaxElpasedMSecs * 2), true);
    QCOMPARE(spyCommLostChanged.count(), 1);
    QCOMPARE(spyCommLostChanged[0][0].toBool(), true);
}

void VehicleLinkManagerTest::_highLatencyLinkTest()
{
    SharedLinkConfigurationPtr mockConfig1;
    SharedLinkInterfacePtr mockLink1;
    SharedLinkConfigurationPtr mockConfig2;
    SharedLinkInterfacePtr mockLink2;

    QSignalSpy spyVehicleCreate(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged);

    _startMockLink(1, true /*highLatency*/, false /*incrementVehicleId*/, mockConfig1, mockLink1);
    MockLink *const pMockLink1 = qobject_cast<MockLink*>(mockLink1.get());

    QCOMPARE(spyVehicleCreate.wait(1000), true);
    QCOMPARE(MultiVehicleManager::instance()->vehicles()->count(), 1);
    Vehicle *const vehicle = MultiVehicleManager::instance()->activeVehicle();
    VehicleLinkManager *const vehicleLinkManager = vehicle->vehicleLinkManager();
    QVERIFY(vehicle);
    QVERIFY(vehicleLinkManager);

    MultiSignalSpyV2 multiSpyVLM;
    QVERIFY(multiSpyVLM.init(vehicleLinkManager));

    // Addition of second non high latency link should:
    //  Change primary link from 1 to 2
    //  Stop high latency transmission on 1

    QSignalSpy spyTransmissionEnabledChanged(pMockLink1, &MockLink::highLatencyTransmissionEnabledChanged);
    QVERIFY(spyTransmissionEnabledChanged.isValid());
    _startMockLink(2, false /*highLatency*/, false /*incrementVehicleId*/, mockConfig2, mockLink2);
    MockLink* pMockLink2 = qobject_cast<MockLink*>(mockLink2.get());

    QCOMPARE(multiSpyVLM.waitForSignal(_primaryLinkChangedSignalName, 100), true);
    QCOMPARE(pMockLink2, vehicleLinkManager->primaryLink().lock().get());
    QCOMPARE(spyTransmissionEnabledChanged.count(), 1);
    QCOMPARE(spyTransmissionEnabledChanged.takeFirst()[0].toBool(), false);
    multiSpyVLM.clearAllSignals();
    spyTransmissionEnabledChanged.clear();

    // Comm lost on primary:2 should:
    //  Switch primary to 1
    //  Re-enable high latency transmission on 1

    pMockLink2->setCommLost(true);
    QCOMPARE(multiSpyVLM.waitForSignal(_primaryLinkChangedSignalName, VehicleLinkManager::_heartbeatMaxElpasedMSecs * 2), true);
    QCOMPARE(pMockLink1, vehicleLinkManager->primaryLink().lock().get());
    QCOMPARE(spyTransmissionEnabledChanged.count(), 1);
    QCOMPARE(spyTransmissionEnabledChanged.takeFirst()[0].toBool(), true);
    spyTransmissionEnabledChanged.clear();
}

void VehicleLinkManagerTest::_startMockLink(int mockIndex, bool highLatency, bool incrementVehicleId, SharedLinkConfigurationPtr &mockConfig, SharedLinkInterfacePtr &mockLink)
{
    MockConfiguration *const pMockConfig = new MockConfiguration(QStringLiteral("Mock %1").arg(mockIndex));

    mockConfig = SharedLinkConfigurationPtr(pMockConfig);

    pMockConfig->setDynamic(true);
    pMockConfig->setHighLatency(highLatency);
    pMockConfig->setIncrementVehicleId(incrementVehicleId);

    QVERIFY(LinkManager::instance()->createConnectedLink(mockConfig));
    QVERIFY(mockConfig->link());

    mockLink = LinkManager::instance()->sharedLinkInterfacePointerForLink(mockConfig->link());
    QVERIFY(mockLink);
}

#include <QtNetwork/QNetworkDatagram>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QtNetwork/QUdpSocket>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "Workers.h"

#ifdef Q_OS_WIN
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

#ifdef Q_OS_HARMONY
#include <QtCore/QElapsedTimer>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>
#include <cstdio>
#endif

namespace {
QByteArray receivedBytes(const QSignalSpy& spy)
{
    QByteArray result;
    for (const auto& signal : spy) {
        result.append(signal.at(0).toByteArray());
    }
    return result;
}

quint16 udpPort(UDPWorker& worker)
{
    return worker.findChild<QUdpSocket*>()->localPort();
}

QByteArray payload(int size, char seed)
{
    QByteArray result(size, Qt::Uninitialized);
    for (int i = 0; i < size; ++i) {
        result[i] = static_cast<char>((i * 17 + seed) & 0xff);
    }
    return result;
}

#if defined(Q_OS_HARMONY) || defined(QGC_TEST_HARMONY_WORKER)
qint64 drainNativeDatagram(QUdpSocket* socket)
{
    char data[256];
#ifdef Q_OS_WIN
    return ::recv(static_cast<SOCKET>(socket->socketDescriptor()), data, sizeof(data), 0);
#else
    return ::recv(static_cast<int>(socket->socketDescriptor()), data, sizeof(data), 0);
#endif
}
#endif
}  // namespace

class NetworkLinksTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void udpBindReceiveAndAutomaticReply();
    void udpManualTargetsAndLearnedDeduplication();
    void udpLearnsMultipleEndpoints();
    void udpEmptyDatagramDoesNotBlockFollowingData();
    void udpLargeDatagramEmitsNoEmptyPayload();
    void udpOccupiedPortReportsFailureAndCanRetry();
    void udpCloseReopenDiscardsPreviousTargets();
    void udpDisconnectedWriteReportsError();
#if defined(Q_OS_HARMONY) || defined(QGC_TEST_HARMONY_WORKER)
    void udpStaleReadyReadPreservesSocketProgress();
    void udpReadRaceRecoversForFollowingDatagrams();
    void udpTemporaryErrorDoesNotMaskRealErrors();
#endif
    void tcpConnectFragmentedReceiveAndBidirectionalLargeData();
    void tcpRefusedConnectionCanReconnect();
    void tcpRemoteDisconnectAndReconnect();
    void tcpLocalDisconnectAndReconnect();
    void tcpInvalidWriteDoesNotPreventSubsequentTransfer();
};

void NetworkLinksTest::initTestCase()
{
    QLoggingCategory::setFilterRules(QStringLiteral("qgc.comms.udplink.debug=false\nqgc.comms.tcplink.debug=false"));
}

void NetworkLinksTest::udpBindReceiveAndAutomaticReply()
{
    UDPConfiguration config;
    UDPWorker worker(&config);
    QSignalSpy connected(&worker, &UDPWorker::connected);
    QSignalSpy received(&worker, &UDPWorker::dataReceived);
    QSignalSpy sent(&worker, &UDPWorker::dataSent);
    QSignalSpy errors(&worker, &UDPWorker::errorOccurred);
    worker.setupSocket();
    worker.connectLink();
    QVERIFY(worker.isConnected());
    QCOMPARE(connected.count(), 1);
    QVERIFY(udpPort(worker) != 0);

    QUdpSocket peer;
    QVERIFY(peer.bind(QHostAddress::LocalHost, 0));
    const QByteArray incoming = QByteArray::fromHex("fd00ff0102030400aabb");
    QCOMPARE(peer.writeDatagram(incoming, QHostAddress::LocalHost, udpPort(worker)), qint64(incoming.size()));
    QTRY_COMPARE_WITH_TIMEOUT(receivedBytes(received), incoming, 2000);
    const QByteArray outgoing("udp-reply\0tail", 14);
    worker.writeData(outgoing);
    QTRY_VERIFY_WITH_TIMEOUT(peer.hasPendingDatagrams(), 2000);
    const auto datagram = peer.receiveDatagram();
    QCOMPARE(datagram.data(), outgoing);
    QVERIFY(datagram.senderAddress().isLoopback());
    QCOMPARE(datagram.senderPort(), int(udpPort(worker)));
    QCOMPARE(sent.count(), 1);
    QCOMPARE(errors.count(), 0);
}

void NetworkLinksTest::udpManualTargetsAndLearnedDeduplication()
{
    QUdpSocket first;
    QUdpSocket second;
    QVERIFY(first.bind(QHostAddress::LocalHost, 0));
    QVERIFY(second.bind(QHostAddress::LocalHost, 0));
    UDPConfiguration config;
    config.addTarget(first.localPort());
    config.addTarget(second.localPort());
    UDPWorker worker(&config);
    QSignalSpy received(&worker, &UDPWorker::dataReceived);
    worker.setupSocket();
    worker.connectLink();
    const QByteArray initial("manual-targets");
    worker.writeData(initial);
    QTRY_VERIFY_WITH_TIMEOUT(first.hasPendingDatagrams() && second.hasPendingDatagrams(), 2000);
    QCOMPARE(first.receiveDatagram().data(), initial);
    QCOMPARE(second.receiveDatagram().data(), initial);

    QCOMPARE(first.writeDatagram("learn", QHostAddress::LocalHost, udpPort(worker)), qint64(5));
    QTRY_COMPARE_WITH_TIMEOUT(receivedBytes(received), QByteArray("learn"), 2000);
    worker.writeData("once-per-endpoint");
    QTRY_VERIFY_WITH_TIMEOUT(first.hasPendingDatagrams() && second.hasPendingDatagrams(), 2000);
    QCOMPARE(first.receiveDatagram().data(), QByteArray("once-per-endpoint"));
    QCOMPARE(second.receiveDatagram().data(), QByteArray("once-per-endpoint"));
    QTest::qWait(25);
    QVERIFY(!first.hasPendingDatagrams());
    QVERIFY(!second.hasPendingDatagrams());
}

void NetworkLinksTest::udpLearnsMultipleEndpoints()
{
    UDPConfiguration config;
    UDPWorker worker(&config);
    QSignalSpy received(&worker, &UDPWorker::dataReceived);
    worker.setupSocket();
    worker.connectLink();
    QUdpSocket first;
    QUdpSocket second;
    QVERIFY(first.bind(QHostAddress::LocalHost, 0));
    QVERIFY(second.bind(QHostAddress::LocalHost, 0));
    QCOMPARE(first.writeDatagram("first", QHostAddress::LocalHost, udpPort(worker)), qint64(5));
    QCOMPARE(second.writeDatagram("second", QHostAddress::LocalHost, udpPort(worker)), qint64(6));
    QTRY_COMPARE_WITH_TIMEOUT(receivedBytes(received).size(), 11, 2000);
    QVERIFY(receivedBytes(received).contains("first"));
    QVERIFY(receivedBytes(received).contains("second"));
    worker.writeData("all-learned-peers");
    QTRY_VERIFY_WITH_TIMEOUT(first.hasPendingDatagrams() && second.hasPendingDatagrams(), 2000);
    QCOMPARE(first.receiveDatagram().data(), QByteArray("all-learned-peers"));
    QCOMPARE(second.receiveDatagram().data(), QByteArray("all-learned-peers"));
}

void NetworkLinksTest::udpEmptyDatagramDoesNotBlockFollowingData()
{
    UDPConfiguration config;
    UDPWorker worker(&config);
    QSignalSpy received(&worker, &UDPWorker::dataReceived);
    QSignalSpy errors(&worker, &UDPWorker::errorOccurred);
    worker.setupSocket();
    worker.connectLink();
    QUdpSocket peer;
    QVERIFY(peer.bind(QHostAddress::LocalHost, 0));
    QCOMPARE(peer.writeDatagram(QByteArray(), QHostAddress::LocalHost, udpPort(worker)), qint64(0));
    QCOMPARE(peer.writeDatagram("valid-after-empty", QHostAddress::LocalHost, udpPort(worker)), qint64(17));
    QTRY_COMPARE_WITH_TIMEOUT(receivedBytes(received), QByteArray("valid-after-empty"), 1000);
    QCOMPARE(errors.count(), 0);
}

void NetworkLinksTest::udpLargeDatagramEmitsNoEmptyPayload()
{
    UDPConfiguration config;
    UDPWorker worker(&config);
    QSignalSpy received(&worker, &UDPWorker::dataReceived);
    worker.setupSocket();
    worker.connectLink();
    QUdpSocket peer;
    QVERIFY(peer.bind(QHostAddress::LocalHost, 0));
    const QByteArray incoming = payload(48000, 7);
    QCOMPARE(peer.writeDatagram(incoming, QHostAddress::LocalHost, udpPort(worker)), qint64(incoming.size()));
    QTRY_COMPARE_WITH_TIMEOUT(receivedBytes(received), incoming, 2000);
    QCOMPARE(received.count(), 1);
    QVERIFY(!received.first().at(0).toByteArray().isEmpty());
}

void NetworkLinksTest::udpOccupiedPortReportsFailureAndCanRetry()
{
    QUdpSocket blocker;
    QVERIFY(blocker.bind(QHostAddress::AnyIPv4, 0, QAbstractSocket::DontShareAddress));
    UDPConfiguration config;
    config.setLocalPort(blocker.localPort());
    UDPWorker worker(&config);
    QSignalSpy connected(&worker, &UDPWorker::connected);
    QSignalSpy errors(&worker, &UDPWorker::errorOccurred);
    worker.setupSocket();
    worker.connectLink();
    QVERIFY(!worker.isConnected());
    QCOMPARE(connected.count(), 0);
    QVERIFY(!errors.isEmpty());
    blocker.close();
    worker.connectLink();
    QVERIFY(worker.isConnected());
    QCOMPARE(udpPort(worker), config.localPort());
    QCOMPARE(connected.count(), 1);
}

void NetworkLinksTest::udpCloseReopenDiscardsPreviousTargets()
{
    UDPConfiguration config;
    UDPWorker worker(&config);
    QSignalSpy received(&worker, &UDPWorker::dataReceived);
    QSignalSpy disconnected(&worker, &UDPWorker::disconnected);
    worker.setupSocket();
    worker.connectLink();
    QUdpSocket peer;
    QVERIFY(peer.bind(QHostAddress::LocalHost, 0));
    QCOMPARE(peer.writeDatagram("old-session", QHostAddress::LocalHost, udpPort(worker)), qint64(11));
    QTRY_COMPARE_WITH_TIMEOUT(receivedBytes(received), QByteArray("old-session"), 2000);
    worker.disconnectLink();
    QVERIFY(!worker.isConnected());
    QVERIFY(!disconnected.isEmpty());
    worker.connectLink();
    QVERIFY(worker.isConnected());
    worker.writeData("must-not-reach-old-peer");
    QTest::qWait(25);
    QVERIFY(!peer.hasPendingDatagrams());
    received.clear();
    QCOMPARE(peer.writeDatagram("new-session", QHostAddress::LocalHost, udpPort(worker)), qint64(11));
    QTRY_COMPARE_WITH_TIMEOUT(receivedBytes(received), QByteArray("new-session"), 2000);
    worker.writeData("new-reply");
    QTRY_VERIFY_WITH_TIMEOUT(peer.hasPendingDatagrams(), 2000);
    QCOMPARE(peer.receiveDatagram().data(), QByteArray("new-reply"));
}

void NetworkLinksTest::udpDisconnectedWriteReportsError()
{
    UDPConfiguration config;
    UDPWorker worker(&config);
    QSignalSpy errors(&worker, &UDPWorker::errorOccurred);
    QSignalSpy sent(&worker, &UDPWorker::dataSent);
    worker.setupSocket();
    worker.writeData("not-connected");
    QCOMPARE(errors.count(), 1);
    QCOMPARE(sent.count(), 0);
    QVERIFY(!worker.isConnected());
}

#if defined(Q_OS_HARMONY) || defined(QGC_TEST_HARMONY_WORKER)
void NetworkLinksTest::udpStaleReadyReadPreservesSocketProgress()
{
    UDPConfiguration config;
    UDPWorker worker(&config);
    QSignalSpy received(&worker, &UDPWorker::dataReceived);
    QSignalSpy errors(&worker, &UDPWorker::errorOccurred);
    worker.setupSocket();
    worker.connectLink();
    QVERIFY(worker.isConnected());
    auto* socket = worker.findChild<QUdpSocket*>();
    QVERIFY(socket);
    QVERIFY(!socket->hasPendingDatagrams());
    QVERIFY(QMetaObject::invokeMethod(socket, "readyRead", Qt::DirectConnection));
    QCOMPARE(received.count(), 0);
#ifdef Q_OS_WIN
    QCOMPARE(errors.count(), 1);  // Qt's Windows backend maps WSAEWOULDBLOCK to NetworkError.
    QCOMPARE(socket->error(), QAbstractSocket::NetworkError);
#else
    QCOMPARE(errors.count(), 0);
    QCOMPARE(socket->error(), QAbstractSocket::TemporaryError);
#endif

    QUdpSocket peer;
    QVERIFY(peer.bind(QHostAddress::LocalHost, 0));
    const QByteArray payload("after-stale-notification");
    QCOMPARE(peer.writeDatagram(payload, QHostAddress::LocalHost, udpPort(worker)), qint64(payload.size()));
    QTRY_COMPARE_WITH_TIMEOUT(receivedBytes(received), payload, 1000);
#ifdef Q_OS_WIN
    QCOMPARE(errors.count(), 1);
#else
    QCOMPARE(errors.count(), 0);
#endif
}

void NetworkLinksTest::udpReadRaceRecoversForFollowingDatagrams()
{
    UDPConfiguration config;
    UDPWorker worker(&config);
    QSignalSpy received(&worker, &UDPWorker::dataReceived);
    QSignalSpy errors(&worker, &UDPWorker::errorOccurred);
    worker.setupSocket();
    worker.connectLink();
    auto* socket = worker.findChild<QUdpSocket*>();
    QVERIFY(socket);
    // Consume the first real packet without clearing Qt's cached readiness.
    // The production Worker then performs a nonblocking read on an empty socket.
    QVERIFY(QObject::disconnect(socket, &QUdpSocket::readyRead, &worker, nullptr));
    bool drained = false;
    qint64 drainedBytes = -1;
    connect(socket, &QUdpSocket::readyRead, &worker, [&]() {
        if (!drained) {
            drainedBytes = drainNativeDatagram(socket);
            drained = true;
        }
    });
    connect(socket, &QUdpSocket::readyRead, &worker,
            [&worker]() { QVERIFY(QMetaObject::invokeMethod(&worker, "_onSocketReadyRead", Qt::DirectConnection)); });
    QUdpSocket peer;
    QVERIFY(peer.bind(QHostAddress::LocalHost, 0));
    const QByteArray raced("removed-before-worker-read");
    QCOMPARE(peer.writeDatagram(raced, QHostAddress::LocalHost, udpPort(worker)), qint64(raced.size()));
    QTRY_VERIFY_WITH_TIMEOUT(drained, 1000);
    QCOMPARE(drainedBytes, qint64(raced.size()));
    QCOMPARE(received.count(), 0);
    QVERIFY(!socket->hasPendingDatagrams());
#ifdef Q_OS_WIN
    QCOMPARE(errors.count(), 1);
    QCOMPARE(socket->error(), QAbstractSocket::NetworkError);
#else
    QCOMPARE(errors.count(), 0);
    QCOMPARE(socket->error(), QAbstractSocket::TemporaryError);
#endif
    QVERIFY(worker.isConnected());

    const QByteArray valid("valid-after-read-race");
    QCOMPARE(peer.writeDatagram(valid, QHostAddress::LocalHost, udpPort(worker)), qint64(valid.size()));
    QTRY_COMPARE_WITH_TIMEOUT(receivedBytes(received), valid, 1000);
    worker.writeData("reply-after-read-race");
    QTRY_VERIFY_WITH_TIMEOUT(peer.hasPendingDatagrams(), 1000);
    QCOMPARE(peer.receiveDatagram().data(), QByteArray("reply-after-read-race"));
#ifdef Q_OS_WIN
    QCOMPARE(errors.count(), 1);
#else
    QCOMPARE(errors.count(), 0);
#endif
}

void NetworkLinksTest::udpTemporaryErrorDoesNotMaskRealErrors()
{
    UDPConfiguration config;
    UDPWorker worker(&config);
    QSignalSpy received(&worker, &UDPWorker::dataReceived);
    QSignalSpy errors(&worker, &UDPWorker::errorOccurred);
    worker.setupSocket();
    worker.connectLink();
    auto* socket = worker.findChild<QUdpSocket*>();
    QVERIFY(socket);
    QVERIFY(QMetaObject::invokeMethod(socket, "errorOccurred", Qt::DirectConnection,
                                      Q_ARG(QAbstractSocket::SocketError, QAbstractSocket::TemporaryError)));
    QCOMPARE(errors.count(), 0);
    QUdpSocket peer;
    QVERIFY(peer.bind(QHostAddress::LocalHost, 0));
    const QByteArray valid("valid-after-temporary-error");
    QCOMPARE(peer.writeDatagram(valid, QHostAddress::LocalHost, udpPort(worker)), qint64(valid.size()));
    QTRY_COMPARE_WITH_TIMEOUT(receivedBytes(received), valid, 1000);
    QCOMPARE(errors.count(), 0);
    QVERIFY(QMetaObject::invokeMethod(socket, "errorOccurred", Qt::DirectConnection,
                                      Q_ARG(QAbstractSocket::SocketError, QAbstractSocket::NetworkError)));
    QCOMPARE(errors.count(), 1);
    QVERIFY(QMetaObject::invokeMethod(socket, "errorOccurred", Qt::DirectConnection,
                                      Q_ARG(QAbstractSocket::SocketError, QAbstractSocket::NetworkError)));
    QCOMPARE(errors.count(), 1);
}
#endif

void NetworkLinksTest::tcpConnectFragmentedReceiveAndBidirectionalLargeData()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    TCPConfiguration config;
    config.setPort(server.serverPort());
    TCPWorker worker(&config);
    QSignalSpy received(&worker, &TCPWorker::dataReceived);
    QSignalSpy connected(&worker, &TCPWorker::connected);
    QSignalSpy sent(&worker, &TCPWorker::dataSent);
    worker.setupSocket();
    worker.connectToHost();
    QVERIFY(worker.isConnected());
    QCOMPARE(connected.count(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 2000);
    const auto peer = server.nextPendingConnection();
    for (const QByteArray& part :
         {QByteArray::fromHex("fd00"), QByteArray::fromHex("ff010203"), QByteArray::fromHex("000004")}) {
        const int previous = receivedBytes(received).size();
        QCOMPARE(peer->write(part), qint64(part.size()));
        QTRY_COMPARE_WITH_TIMEOUT(receivedBytes(received).size(), previous + part.size(), 2000);
    }
    QCOMPARE(receivedBytes(received), QByteArray::fromHex("fd00ff010203000004"));
    received.clear();
    const QByteArray outgoing = payload(256 * 1024, 3);
    worker.writeData(outgoing);
    QTRY_COMPARE_WITH_TIMEOUT(peer->bytesAvailable(), qint64(outgoing.size()), 5000);
    QCOMPARE(peer->readAll(), outgoing);
    QCOMPARE(receivedBytes(sent), outgoing);
    const QByteArray incoming = payload(256 * 1024, 11);
    QCOMPARE(peer->write(incoming), qint64(incoming.size()));
    QTRY_COMPARE_WITH_TIMEOUT(receivedBytes(received), incoming, 5000);
}

void NetworkLinksTest::tcpRefusedConnectionCanReconnect()
{
    QTcpServer reservation;
    QVERIFY(reservation.listen(QHostAddress::LocalHost, 0));
    const quint16 port = reservation.serverPort();
    reservation.close();
    TCPConfiguration config;
    config.setPort(port);
    TCPWorker worker(&config);
    QSignalSpy errors(&worker, &TCPWorker::errorOccurred);
    QSignalSpy connected(&worker, &TCPWorker::connected);
    worker.setupSocket();
    worker.connectToHost();
    QVERIFY(!worker.isConnected());
    QCOMPARE(errors.count(), 1);
    QCOMPARE(connected.count(), 0);
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, port));
    worker.connectToHost();
    QVERIFY(worker.isConnected());
    QCOMPARE(connected.count(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 2000);
    const auto peer = server.nextPendingConnection();
    worker.writeData("recovered");
    QTRY_COMPARE_WITH_TIMEOUT(peer->bytesAvailable(), qint64(9), 2000);
    QCOMPARE(peer->readAll(), QByteArray("recovered"));
}

void NetworkLinksTest::tcpRemoteDisconnectAndReconnect()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    TCPConfiguration config;
    config.setPort(server.serverPort());
    TCPWorker worker(&config);
    QSignalSpy disconnected(&worker, &TCPWorker::disconnected);
    QSignalSpy received(&worker, &TCPWorker::dataReceived);
    worker.setupSocket();
    worker.connectToHost();
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 2000);
    auto peer = server.nextPendingConnection();
    peer->disconnectFromHost();
    QTRY_VERIFY_WITH_TIMEOUT(!worker.isConnected(), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(disconnected.count(), 1, 2000);
    worker.connectToHost();
    QVERIFY(worker.isConnected());
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 2000);
    peer = server.nextPendingConnection();
    QCOMPARE(peer->write("second-connection"), qint64(17));
    QTRY_COMPARE_WITH_TIMEOUT(receivedBytes(received), QByteArray("second-connection"), 2000);
}

void NetworkLinksTest::tcpLocalDisconnectAndReconnect()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    TCPConfiguration config;
    config.setPort(server.serverPort());
    TCPWorker worker(&config);
    QSignalSpy disconnected(&worker, &TCPWorker::disconnected);
    worker.setupSocket();
    worker.connectToHost();
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 2000);
    const auto first = server.nextPendingConnection();
    worker.disconnectFromHost();
    QTRY_VERIFY_WITH_TIMEOUT(!worker.isConnected(), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(first->state(), QAbstractSocket::UnconnectedState, 2000);
    QCOMPARE(disconnected.count(), 1);
    worker.connectToHost();
    QVERIFY(worker.isConnected());
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 2000);
    const auto second = server.nextPendingConnection();
    worker.writeData("after-local-close");
    QTRY_COMPARE_WITH_TIMEOUT(second->bytesAvailable(), qint64(17), 2000);
    QCOMPARE(second->readAll(), QByteArray("after-local-close"));
}

void NetworkLinksTest::tcpInvalidWriteDoesNotPreventSubsequentTransfer()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    TCPConfiguration config;
    config.setPort(server.serverPort());
    TCPWorker worker(&config);
    QSignalSpy errors(&worker, &TCPWorker::errorOccurred);
    QSignalSpy sent(&worker, &TCPWorker::dataSent);
    worker.setupSocket();
    worker.writeData("disconnected");
    QCOMPARE(errors.count(), 1);
    QCOMPARE(sent.count(), 0);
    worker.connectToHost();
    QVERIFY(worker.isConnected());
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 2000);
    const auto peer = server.nextPendingConnection();
    worker.writeData(QByteArray());
    QCOMPARE(errors.count(), 2);
    QCOMPARE(sent.count(), 0);
    QVERIFY(worker.isConnected());
    worker.writeData("valid-after-error");
    QTRY_COMPARE_WITH_TIMEOUT(peer->bytesAvailable(), qint64(17), 2000);
    QCOMPARE(peer->readAll(), QByteArray("valid-after-error"));
    QCOMPARE(sent.count(), 1);
}

#ifdef Q_OS_HARMONY
int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QWindow window;
    window.setTitle(QStringLiteral("QGC Network Tests"));
    window.resize(640, 360);
    window.show();
    window.requestActivate();
    const QString mode = qEnvironmentVariable("QGC_NETWORK_RUN");
    QElapsedTimer elapsed;
    elapsed.start();
    QTimer ready;
    ready.setInterval(100);
    QObject::connect(&ready, &QTimer::timeout, &app, [&]() {
        if (!window.isExposed()) {
            if (elapsed.elapsed() < 15000)
                return;
            ready.stop();
            std::fprintf(stdout, "QGC_NETWORK_DONE exit=2 window-not-ready\n");
            std::fflush(stdout);
            app.exit(2);
            return;
        }
        ready.stop();
        std::fprintf(stdout, "QGC_NETWORK_READY mode=%s\n", mode.isEmpty() ? "idle" : "loopback");
        std::fflush(stdout);
        if (mode.isEmpty())
            return;
        int result = 2;
        if (mode == QStringLiteral("1")) {
            NetworkLinksTest test;
            result = QTest::qExec(&test, app.arguments());
        }
        std::fprintf(stdout, "QGC_NETWORK_DONE exit=%d\n", result);
        std::fflush(stdout);
        app.exit(result);
    });
    ready.start();
    return app.exec();
}
#else
QTEST_GUILESS_MAIN(NetworkLinksTest)
#endif
#include "NetworkLinksTest.moc"

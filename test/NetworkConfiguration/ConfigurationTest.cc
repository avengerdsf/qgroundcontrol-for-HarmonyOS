#include "Configuration.h"

#include <QtCore/QTemporaryDir>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

class ConfigurationTest : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void udpDefaultsAndMissingSettingsUseListenPreference();
    void udpTargetsParseResolveDeduplicateAndRemove();
    void udpCopyConstructorOwnsIndependentTargets();
    void udpCopyFromReplacesTargets();
    void udpCopyFromEmptyClearsVisibleHostList();
    void udpIniRoundTripPreservesTargetsAndLocalPort();
    void udpLoadDeduplicatesTargetsAndClearsPreviousState();
    void udpAutoConnectUsesSettingsAndPreservesManualTargets();
    void udpAutoConnectWithoutTargetOnlyChangesListenPort();
    void tcpDefaultsAndMissingSettings();
    void tcpCopyConstructorAndCopyFromPreserveEndpoint();
    void tcpIniRoundTripPreservesHostAndPort();
};

void ConfigurationTest::init()
{
    AutoConnectSettings *const settings = SettingsManager::instance()->autoConnectSettings();
    settings->udpListenPort()->setRawValue(14550);
    settings->udpTargetHostIP()->setRawValue(QString());
    settings->udpTargetHostPort()->setRawValue(14550);
}

void ConfigurationTest::udpDefaultsAndMissingSettingsUseListenPreference()
{
    UDPConfiguration configuration(QStringLiteral("UDP"));
    QCOMPARE(configuration.type(), LinkConfiguration::TypeUdp);
    QCOMPARE(configuration.localPort(), quint16(0));
    QVERIFY(configuration.hostList().isEmpty());
    QVERIFY(configuration.targetHosts().isEmpty());
    QVERIFY(!configuration.isAutoConnect());

    QTemporaryDir directory(QStringLiteral(TEST_BINARY_DIR "/ini-XXXXXX"));
    QVERIFY(directory.isValid());
    QSettings ini(directory.filePath(QStringLiteral("udp.ini")), QSettings::IniFormat);
    SettingsManager::instance()->autoConnectSettings()->udpListenPort()->setRawValue(14600);
    configuration.loadSettings(ini, QStringLiteral("missing"));
    QCOMPARE(configuration.localPort(), quint16(14600));
    QVERIFY(configuration.hostList().isEmpty());
    QCOMPARE(ini.group(), QString());
}

void ConfigurationTest::udpTargetsParseResolveDeduplicateAndRemove()
{
    UDPConfiguration configuration(QStringLiteral("UDP"));
    configuration.setLocalPort(14550);
    QSignalSpy hosts(&configuration, &UDPConfiguration::hostListChanged);
    configuration.addHost(QStringLiteral("127.0.0.1"));
    configuration.addHost(QStringLiteral("localhost"), 14550);
    configuration.addHost(QStringLiteral("127.0.0.1:14551"));
    configuration.addHost(QStringLiteral("127.0.0.1"), 14551);
    QCOMPARE(configuration.targetHosts().size(), 2);
    QCOMPARE(configuration.hostList(), QStringList({QStringLiteral("127.0.0.1:14550"), QStringLiteral("127.0.0.1:14551")}));
    QCOMPARE(hosts.count(), 2);
    QCOMPARE(configuration.targetHosts().at(0)->address, QHostAddress(QHostAddress::LocalHost));
    QCOMPARE(configuration.targetHosts().at(0)->port, quint16(14550));

    configuration.removeHost(QStringLiteral("localhost"));
    QCOMPARE(configuration.hostList(), QStringList({QStringLiteral("127.0.0.1:14551")}));
    configuration.removeHost(QStringLiteral("127.0.0.1:14551"));
    QVERIFY(configuration.hostList().isEmpty());
    QVERIFY(configuration.targetHosts().isEmpty());
    configuration.addHost(QStringLiteral("127.0.0.1"), 14552);
    configuration.removeHost(QStringLiteral("localhost"), 14552);
    QVERIFY(configuration.targetHosts().isEmpty());
}

void ConfigurationTest::udpCopyConstructorOwnsIndependentTargets()
{
    UDPConfiguration source(QStringLiteral("source"));
    source.setLocalPort(14551);
    source.addHost(QStringLiteral("127.0.0.1:14552"));
    UDPConfiguration copy(&source);
    QCOMPARE(copy.localPort(), quint16(14551));
    QCOMPARE(copy.hostList(), QStringList({QStringLiteral("127.0.0.1:14552")}));
    QVERIFY(copy.targetHosts().first().get() != source.targetHosts().first().get());
    source.removeHost(QStringLiteral("127.0.0.1:14552"));
    QCOMPARE(copy.hostList(), QStringList({QStringLiteral("127.0.0.1:14552")}));
    copy.targetHosts().first()->port = 14553;
    QVERIFY(source.targetHosts().isEmpty());
}

void ConfigurationTest::udpCopyFromReplacesTargets()
{
    UDPConfiguration source(QStringLiteral("source"));
    source.setLocalPort(14560);
    source.addHost(QStringLiteral("127.0.0.1:14561"));
    UDPConfiguration destination(QStringLiteral("destination"));
    destination.addHost(QStringLiteral("127.0.0.1:14562"));
    destination.copyFrom(&source);
    QCOMPARE(destination.localPort(), quint16(14560));
    QCOMPARE(destination.hostList(), QStringList({QStringLiteral("127.0.0.1:14561")}));
    QCOMPARE(destination.targetHosts().size(), 1);
    QVERIFY(destination.targetHosts().first().get() != source.targetHosts().first().get());
}

void ConfigurationTest::udpCopyFromEmptyClearsVisibleHostList()
{
    UDPConfiguration source(QStringLiteral("empty"));
    UDPConfiguration destination(QStringLiteral("destination"));
    destination.addHost(QStringLiteral("127.0.0.1:14550"));
    QSignalSpy changed(&destination, &UDPConfiguration::hostListChanged);
    destination.copyFrom(&source);
    QVERIFY(destination.targetHosts().isEmpty());
    QVERIFY2(destination.hostList().isEmpty(), "Copying an empty configuration must clear the host list shown by QML");
    QVERIFY(changed.count() > 0);
}

void ConfigurationTest::udpIniRoundTripPreservesTargetsAndLocalPort()
{
    QTemporaryDir directory(QStringLiteral(TEST_BINARY_DIR "/ini-XXXXXX"));
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("udp.ini"));
    {
        UDPConfiguration source(QStringLiteral("source"));
        source.setLocalPort(14610);
        source.addHost(QStringLiteral("127.0.0.1:14611"));
        source.addHost(QStringLiteral("localhost"), 14612);
        QSettings ini(path, QSettings::IniFormat);
        ini.setValue(QStringLiteral("unrelated/value"), QStringLiteral("keep"));
        source.saveSettings(ini, QStringLiteral("links/udp"));
        QCOMPARE(ini.group(), QString());
        ini.sync();
        QCOMPARE(ini.status(), QSettings::NoError);
    }
    UDPConfiguration restored(QStringLiteral("restored"));
    QSettings ini(path, QSettings::IniFormat);
    restored.loadSettings(ini, QStringLiteral("links/udp"));
    QCOMPARE(restored.localPort(), quint16(14610));
    QCOMPARE(restored.hostList(), QStringList({QStringLiteral("127.0.0.1:14611"), QStringLiteral("127.0.0.1:14612")}));
    QCOMPARE(restored.targetHosts().size(), 2);
    QCOMPARE(ini.value(QStringLiteral("unrelated/value")).toString(), QStringLiteral("keep"));
    QCOMPARE(ini.group(), QString());
}

void ConfigurationTest::udpLoadDeduplicatesTargetsAndClearsPreviousState()
{
    QTemporaryDir directory(QStringLiteral(TEST_BINARY_DIR "/ini-XXXXXX"));
    QVERIFY(directory.isValid());
    QSettings ini(directory.filePath(QStringLiteral("udp.ini")), QSettings::IniFormat);
    ini.setValue(QStringLiteral("udp/port"), 14620);
    ini.setValue(QStringLiteral("udp/hostCount"), 3);
    ini.setValue(QStringLiteral("udp/host0"), QStringLiteral("127.0.0.1"));
    ini.setValue(QStringLiteral("udp/port0"), 14621);
    ini.setValue(QStringLiteral("udp/host1"), QStringLiteral("localhost"));
    ini.setValue(QStringLiteral("udp/port1"), 14621);
    ini.setValue(QStringLiteral("udp/host2"), QStringLiteral("127.0.0.1"));
    UDPConfiguration configuration(QStringLiteral("UDP"));
    configuration.addHost(QStringLiteral("127.0.0.1:14622"));
    configuration.loadSettings(ini, QStringLiteral("udp"));
    QCOMPARE(configuration.localPort(), quint16(14620));
    QCOMPARE(configuration.hostList(), QStringList({QStringLiteral("127.0.0.1:14621")}));
    QCOMPARE(configuration.targetHosts().size(), 1);
    configuration.loadSettings(ini, QStringLiteral("empty"));
    QVERIFY(configuration.targetHosts().isEmpty());
    QVERIFY(configuration.hostList().isEmpty());
}

void ConfigurationTest::udpAutoConnectUsesSettingsAndPreservesManualTargets()
{
    AutoConnectSettings *const settings = SettingsManager::instance()->autoConnectSettings();
    settings->udpListenPort()->setRawValue(14630);
    settings->udpTargetHostIP()->setRawValue(QStringLiteral("localhost"));
    settings->udpTargetHostPort()->setRawValue(14631);
    UDPConfiguration configuration(QStringLiteral("UDP"));
    configuration.addHost(QStringLiteral("127.0.0.1:14632"));
    QSignalSpy changed(&configuration, &LinkConfiguration::autoConnectChanged);
    configuration.setAutoConnect(true);
    QVERIFY(configuration.isAutoConnect());
    QCOMPARE(configuration.localPort(), quint16(14630));
    QCOMPARE(configuration.hostList(), QStringList({QStringLiteral("127.0.0.1:14632"), QStringLiteral("127.0.0.1:14631")}));
    configuration.setAutoConnect(true);
    QCOMPARE(configuration.targetHosts().size(), 2);
    QCOMPARE(changed.count(), 1);
    configuration.setAutoConnect(false);
    QVERIFY(!configuration.isAutoConnect());
    QCOMPARE(configuration.localPort(), quint16(0));
    QCOMPARE(configuration.hostList(), QStringList({QStringLiteral("127.0.0.1:14632")}));
    QCOMPARE(changed.count(), 2);
}

void ConfigurationTest::udpAutoConnectWithoutTargetOnlyChangesListenPort()
{
    UDPConfiguration configuration(QStringLiteral("UDP"));
    configuration.setAutoConnect();
    QVERIFY(configuration.isAutoConnect());
    QCOMPARE(configuration.localPort(), quint16(14550));
    QVERIFY(configuration.targetHosts().isEmpty());
    configuration.setAutoConnect(false);
    QVERIFY(!configuration.isAutoConnect());
    QCOMPARE(configuration.localPort(), quint16(0));
    QVERIFY(configuration.hostList().isEmpty());
}

void ConfigurationTest::tcpDefaultsAndMissingSettings()
{
    TCPConfiguration configuration(QStringLiteral("TCP"));
    QCOMPARE(configuration.type(), LinkConfiguration::TypeTcp);
    QCOMPARE(configuration.host(), QString());
    QCOMPARE(configuration.port(), quint16(5760));
    QTemporaryDir directory(QStringLiteral(TEST_BINARY_DIR "/ini-XXXXXX"));
    QVERIFY(directory.isValid());
    QSettings ini(directory.filePath(QStringLiteral("tcp.ini")), QSettings::IniFormat);
    configuration.setHost(QStringLiteral("127.0.0.1"));
    configuration.setPort(14640);
    configuration.loadSettings(ini, QStringLiteral("missing"));
    QCOMPARE(configuration.host(), QStringLiteral("127.0.0.1"));
    QCOMPARE(configuration.port(), quint16(14640));
    QCOMPARE(ini.group(), QString());
}

void ConfigurationTest::tcpCopyConstructorAndCopyFromPreserveEndpoint()
{
    TCPConfiguration source(QStringLiteral("source"));
    source.setHost(QStringLiteral("127.0.0.1"));
    source.setPort(14641);
    TCPConfiguration copy(&source);
    QCOMPARE(copy.host(), QStringLiteral("127.0.0.1"));
    QCOMPARE(copy.port(), quint16(14641));
    TCPConfiguration destination(QStringLiteral("destination"));
    destination.setPort(14642);
    destination.copyFrom(&source);
    QCOMPARE(destination.host(), QStringLiteral("127.0.0.1"));
    QCOMPARE(destination.port(), quint16(14641));
    source.setPort(14643);
    QCOMPARE(copy.port(), quint16(14641));
    QCOMPARE(destination.port(), quint16(14641));
}

void ConfigurationTest::tcpIniRoundTripPreservesHostAndPort()
{
    QTemporaryDir directory(QStringLiteral(TEST_BINARY_DIR "/ini-XXXXXX"));
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("tcp.ini"));
    {
        TCPConfiguration source(QStringLiteral("source"));
        source.setHost(QStringLiteral("127.0.0.1"));
        source.setPort(14644);
        QSettings ini(path, QSettings::IniFormat);
        source.saveSettings(ini, QStringLiteral("links/tcp"));
        QCOMPARE(ini.group(), QString());
        ini.sync();
        QCOMPARE(ini.status(), QSettings::NoError);
    }
    TCPConfiguration restored(QStringLiteral("restored"));
    QSettings ini(path, QSettings::IniFormat);
    restored.loadSettings(ini, QStringLiteral("links/tcp"));
    QCOMPARE(restored.host(), QStringLiteral("127.0.0.1"));
    QCOMPARE(restored.port(), quint16(14644));
    QCOMPARE(ini.group(), QString());
}

QTEST_GUILESS_MAIN(ConfigurationTest)
#include "ConfigurationTest.moc"

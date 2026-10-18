#include <QtCore/QObject>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickItem>
#include <QtTest/QTest>
#include <memory>

class ConfigurationFixture : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int mode MEMBER mode NOTIFY changed)
    Q_PROPERTY(bool nativeBluetooth MEMBER native NOTIFY changed)
    Q_PROPERTY(QString serviceUuid MEMBER service NOTIFY changed)
    Q_PROPERTY(QString readUuid MEMBER read NOTIFY changed)
    Q_PROPERTY(QString writeUuid MEMBER write NOTIFY changed)
    Q_PROPERTY(QString address MEMBER address NOTIFY changed)
    Q_PROPERTY(QString deviceName MEMBER device NOTIFY changed)
    Q_PROPERTY(QVariantList devicesModel MEMBER devices NOTIFY changed)
    Q_PROPERTY(bool scanning MEMBER scanning NOTIFY changed)
    Q_PROPERTY(bool adapterAvailable MEMBER available NOTIFY changed)
    Q_PROPERTY(bool adapterPoweredOn MEMBER powered NOTIFY changed)
    Q_PROPERTY(bool adapterDiscoverable MEMBER discoverable NOTIFY changed)
    Q_PROPERTY(QString adapterAddress MEMBER adapterAddress NOTIFY changed)
    Q_PROPERTY(QString hostMode MEMBER hostMode NOTIFY changed)
    Q_PROPERTY(int connectedRssi MEMBER connectedRssi NOTIFY changed)
    Q_PROPERTY(int selectedRssi MEMBER selectedRssi NOTIFY changed)
public:
    enum class BluetoothMode
    {
        ModeClassic,
        ModeLowEnergy
    };
    Q_ENUM(BluetoothMode)
    int mode = 1;
    bool native = true;
    QString service = "{6e400001-b5a3-f393-e0a9-e50e24dcca9e}";
    QString read = "{6e400003-b5a3-f393-e0a9-e50e24dcca9e}";
    QString write = "{6e400002-b5a3-f393-e0a9-e50e24dcca9e}";
    QString address;
    QString device;
    QVariantList devices{QVariantMap{{"name", "Radio"}, {"address", "12:34:56:78:90:AB"}, {"rssi", -60}},
                         QVariantMap{{"name", "Radio"}, {"address", "12:34:56:78:90:AC"}, {"rssi", -40}}};
    bool scanning = false;
    bool available = true;
    bool powered = false;
    bool discoverable = false;
    QString adapterAddress;
    QString hostMode = "Managed in system settings";
    int connectedRssi = 0;
    int selectedRssi = -60;
    int scanCalls = 0;
    int stopCalls = 0;

    Q_INVOKABLE QVariantList getConnectedDevices() const { return {}; }

    Q_INVOKABLE QVariantList getAllPairedDevices() const { return {}; }

    Q_INVOKABLE QVariantList getAllAvailableAdapters() const { return {}; }

    Q_INVOKABLE bool isPaired(const QString&) const { return false; }

    Q_INVOKABLE QString getPairingStatus(const QString&) const { return "Managed in system settings"; }

    Q_INVOKABLE void selectAdapter(const QString&) {}

    Q_INVOKABLE void startScan()
    {
        ++scanCalls;
        scanning = true;
        emit changed();
    }

    Q_INVOKABLE void stopScan()
    {
        ++stopCalls;
        scanning = false;
        emit changed();
    }

    Q_INVOKABLE void setDeviceByAddress(const QString& value)
    {
        address = value;
        device = "Radio";
        stopScan();
        emit deviceChanged();
    }

signals:
    void changed();
    void deviceChanged();
    void devicesModelChanged();
    void modeChanged();
    void adapterStateChanged();
    void pairingStatusChanged();
    void errorOccurred(const QString& message);
};

class EditFixture : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name MEMBER name NOTIFY changed)
    Q_PROPERTY(QString settingsURL MEMBER settingsURL NOTIFY changed)
    Q_PROPERTY(int linkType MEMBER linkType NOTIFY changed)
    Q_PROPERTY(bool dynamic MEMBER dynamicConfig NOTIFY changed)
    Q_PROPERTY(bool autoConnect MEMBER autoConnect NOTIFY changed)
    Q_PROPERTY(bool highLatency MEMBER highLatency NOTIFY changed)
public:
    QString name = "Radio";
    QString settingsURL;
    int linkType = 0;
    bool dynamicConfig = true;
    bool autoConnect = false;
    bool highLatency = false;
signals:
    void changed();
};

class LinkManagerFixture : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList linkConfigurations MEMBER configurations NOTIFY changed)
    Q_PROPERTY(QStringList linkTypeStrings MEMBER types NOTIFY changed)
public:
    QVariantList configurations;
    QStringList types{"Bluetooth"};
    int saved = 0;

    Q_INVOKABLE void endCreateConfiguration(QObject*) { ++saved; }

    Q_INVOKABLE void cancelConfigurationEditing(QObject*) {}

signals:
    void changed();
};

class QgcFixture : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject* linkManager READ linkManager CONSTANT)
public:
    LinkManagerFixture manager;

    QObject* linkManager() { return &manager; }

    QString lastError;

    Q_INVOKABLE void showMessageDialog(QObject*, const QString&, const QString& error, int) { lastError = error; }
};

class BluetoothSettingsTest : public QObject
{
    Q_OBJECT
private slots:

    void productionPageUsesNewModelAndValidatesUuids()
    {
        QQmlEngine engine;
        ConfigurationFixture config;
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(PRODUCT_QML_PATH)));
        std::unique_ptr<QObject> page(
            component.createWithInitialProperties({{"subEditConfig", QVariant::fromValue(&config)}}));
        QVERIFY2(page, qPrintable(component.errorString()));
        QVERIFY(page->property("isBleMode").toBool());
        QVERIFY(page->property("settingsValid").toBool());
        const QVariantList sorted = page->property("sortedDevices").value<QJSValue>().toVariant().toList();
        QCOMPARE(sorted.size(), 2);
        QCOMPARE(sorted.first().toMap().value("address").toString(), QStringLiteral("12:34:56:78:90:AC"));
        QObject* service = page->findChild<QObject*>("serviceUuidField");
        QVERIFY(service);
        QVERIFY(service->setProperty("text", "broken-uuid"));
        QVERIFY(!page->property("settingsValid").toBool());
        const QString saved = config.service;
        QVariant accepted;
        QVERIFY(QMetaObject::invokeMethod(page.get(), "saveSettings", Q_RETURN_ARG(QVariant, accepted)));
        QVERIFY(!accepted.toBool());
        QCOMPARE(config.service, saved);
        for (const char* invalid :
             {"{6e400001-b5a3-f393-e0a9-e50e24dcca9e", "6e400001-b5a3-f393-e0a9-e50e24dcca9e}", "0x12", "+123"}) {
            QVERIFY(service->setProperty("text", invalid));
            QVERIFY(!page->property("settingsValid").toBool());
        }
        QVERIFY(service->setProperty("text", " FFE0 "));
        QVERIFY(page->property("settingsValid").toBool());
        QVERIFY(QMetaObject::invokeMethod(page.get(), "saveSettings", Q_RETURN_ARG(QVariant, accepted)));
        QVERIFY(accepted.toBool());
        QCOMPARE(config.service, QStringLiteral("FFE0"));
        QVERIFY(service->setProperty("text", ""));
        QVERIFY(!page->property("settingsValid").toBool());
        config.native = false;
        emit config.changed();
        QVERIFY(page->property("settingsValid").toBool());
    }

    void nativeSystemControlsAndPermissionFeedbackRemainVisible()
    {
        QQmlEngine engine;
        ConfigurationFixture config;
        config.mode = 0;
        config.address = "12:34:56:78:90:AB";
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(PRODUCT_QML_PATH)));
        std::unique_ptr<QObject> page(
            component.createWithInitialProperties({{"subEditConfig", QVariant::fromValue(&config)}}));
        QVERIFY2(page, qPrintable(component.errorString()));
        bool pairingDisabled = false;
        for (QObject* control : page->findChildren<QObject*>()) {
            if (control->property("text").toString() == "Pair") {
                pairingDisabled = !control->property("enabled").toBool();
            }
        }
        QVERIFY(pairingDisabled);
        auto* qgc = engine.singletonInstance<QgcFixture*>(qmlTypeId("QGroundControl", 1, 0, "QGroundControl"));
        QVERIFY(qgc);
        emit config.errorOccurred("Bluetooth access was denied (201)");
        QCOMPARE(qgc->lastError, QStringLiteral("Bluetooth access was denied (201)"));
        QVERIFY(page->property("settingsValid").toBool());
    }

    void actualParentDialogValidatesBeforeSaving_data()
    {
        QTest::addColumn<QString>("pageKind");
        QTest::addColumn<bool>("enabled");
        QTest::addColumn<bool>("saved");
        QTest::newRow("legacy-void") << QStringLiteral("Legacy") << true << true;
        QTest::newRow("no-save-method") << QStringLiteral("NoSaveMethod") << true << true;
        QTest::newRow("invalid-input") << QStringLiteral("Invalid") << false << false;
        QTest::newRow("save-rejected") << QStringLiteral("RejectSave") << true << false;
        QTest::newRow("missing-loader-page") << QStringLiteral("Missing") << false << false;
        QTest::newRow("valid") << QStringLiteral("Valid") << true << true;
    }

    void actualParentDialogValidatesBeforeSaving()
    {
        QFETCH(QString, pageKind);
        QFETCH(bool, enabled);
        QFETCH(bool, saved);
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(PRODUCT_PARENT_QML_PATH)));
        std::unique_ptr<QObject> parent(component.create());
        QVERIFY2(parent, qPrintable(component.errorString()));
        QObject* factory = nullptr;
        for (QObject* child : parent->findChildren<QObject*>()) {
            if (QString::fromLatin1(child->metaObject()->className()).startsWith("QGCPopupDialogFactory")) {
                factory = child;
                break;
            }
        }
        QVERIFY(factory);
        EditFixture edit;
        edit.settingsURL =
            pageKind == "Missing"
                ? QString()
                : QUrl::fromLocalFile(QStringLiteral(TEST_BINARY_DIR "/") + pageKind + "LinkSettings.qml").toString();
        QVariant dialogValue;
        QVariant props = QVariantMap{{"editingConfig", QVariant::fromValue(&edit)},
                                     {"originalConfig", QVariant::fromValue<QObject*>(nullptr)}};
        QVERIFY(
            QMetaObject::invokeMethod(factory, "open", Q_RETURN_ARG(QVariant, dialogValue), Q_ARG(QVariant, props)));
        QObject* dialog = dialogValue.value<QObject*>();
        if (!dialog) {
            dialog = dialogValue.value<QJSValue>().toQObject();
        }
        QVERIFY(dialog);
        QObject* loader = nullptr;
        for (QObject* child : dialog->findChildren<QObject*>()) {
            if (QString::fromLatin1(child->metaObject()->className()).startsWith("QQuickLoader")) {
                loader = child;
                break;
            }
        }
        QVERIFY(loader);
        QTRY_COMPARE(loader->property("status").toInt(), pageKind == "Missing" ? 0 : 1);
        QCOMPARE(dialog->property("acceptButtonEnabled").toBool(), enabled);
        auto* qgc = engine.singletonInstance<QgcFixture*>(qmlTypeId("QGroundControl", 1, 0, "QGroundControl"));
        QVERIFY(qgc);
        QVERIFY(QMetaObject::invokeMethod(dialog, "accepted"));
        QCOMPARE(qgc->manager.saved, saved ? 1 : 0);
        QCOMPARE(dialog->property("preventClose").toBool(), !saved);
        QCOMPARE(edit.dynamicConfig, !saved);
    }
};

int main(int argc, char** argv)
{
    QGuiApplication application(argc, argv);
    qmlRegisterModule("QGroundControl", 1, 0);
    qmlRegisterModule("QGroundControl.Controls", 1, 0);
    qmlRegisterModule("QGroundControl.FactControls", 1, 0);
    qmlRegisterType(QUrl::fromLocalFile(QStringLiteral(PRODUCT_FACTORY_QML_PATH)), "QGroundControl.Controls", 1, 0,
                    "QGCPopupDialogFactory");
    qmlRegisterUncreatableType<ConfigurationFixture>("QGroundControl", 1, 0, "BluetoothConfiguration", "test enum");
    qmlRegisterSingletonType<QgcFixture>("QGroundControl", 1, 0, "QGroundControl",
                                         [](QQmlEngine*, QJSEngine*) { return new QgcFixture; });
    qmlRegisterSingletonType(QUrl::fromLocalFile(QStringLiteral(TEST_BINARY_DIR "/ScreenTools.qml")), "QGroundControl",
                             1, 0, "ScreenTools");
    qmlRegisterType(QUrl::fromLocalFile(QStringLiteral(TEST_BINARY_DIR "/QGCPalette.qml")), "QGroundControl", 1, 0,
                    "QGCPalette");
    for (const char* name :
         {"QGCButton", "QGCLabel", "QGCComboBox", "QGCTextField", "QGCCheckBox", "QGCRadioButton", "SectionHeader",
          "QGCFlickable", "SettingsGroupLayout", "LabelledButton", "QGCColoredImage", "QGCMouseArea", "QGCPopupDialog",
          "LabelledComboBox", "QGCCheckBoxSlider"}) {
        qmlRegisterType(QUrl::fromLocalFile(QStringLiteral(TEST_BINARY_DIR "/") + QString::fromLatin1(name) + ".qml"),
                        "QGroundControl.Controls", 1, 0, name);
    }
    BluetoothSettingsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "BluetoothSettingsTest.moc"

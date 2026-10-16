#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QObject>
#include <QtCore/QRegularExpression>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQml/QQmlExpression>
#include <QtQml/QQmlPropertyMap>
#include <QtQuick/QQuickItem>
#include <QtTest/QTest>

#include <memory>

class BluetoothConfigurationFixture : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supportsLowEnergy MEMBER supported NOTIFY changed)
    Q_PROPERTY(bool lowEnergy MEMBER lowEnergy NOTIFY changed)
    Q_PROPERTY(QString bleServiceUuid MEMBER service NOTIFY changed)
    Q_PROPERTY(QString bleWriteUuid MEMBER write NOTIFY changed)
    Q_PROPERTY(QString bleNotifyUuid MEMBER notify NOTIFY changed)
    Q_PROPERTY(QString deviceName MEMBER device NOTIFY changed)
    Q_PROPERTY(QString address MEMBER address NOTIFY changed)
    Q_PROPERTY(QStringList nameList MEMBER names NOTIFY changed)
    Q_PROPERTY(bool scanning MEMBER scanning NOTIFY changed)
public:
    bool supported = true;
    bool lowEnergy = false;
    QString service = QStringLiteral("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
    QString write = QStringLiteral("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
    QString notify = QStringLiteral("6e400003-b5a3-f393-e0a9-e50e24dcca9e");
    QString device;
    QString address;
    QStringList names{QStringLiteral("Radio A"), QStringLiteral("Radio B")};
    bool scanning = false;
    int scanCalls = 0;
    int stopCalls = 0;

    Q_INVOKABLE void startScan() { ++scanCalls; scanning = true; emit changed(); }
    Q_INVOKABLE void stopScan() { ++stopCalls; scanning = false; emit changed(); }
    Q_INVOKABLE void setDevice(const QString &name) { device = name; address = QStringLiteral("12:34:56:78:90:AB"); emit changed(); }
    void failDiscovery(const QString &message) { emit errorOccurred(message); scanning = false; emit changed(); }
signals:
    void changed();
    void errorOccurred(const QString &errorString);
};

class SettingsValidityFixture : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool settingsValid MEMBER valid)
public:
    bool valid = false;
};

struct SettingsPage
{
    BluetoothConfigurationFixture configuration;
    QQmlEngine engine;
    QQmlComponent component{&engine};
    std::unique_ptr<QObject> object;

    explicit SettingsPage(bool lowEnergy = false, bool supported = true)
    {
        configuration.lowEnergy = lowEnergy;
        configuration.supported = supported;
        engine.rootContext()->setContextProperty("subEditConfig", &configuration);
        engine.rootContext()->setContextProperty("_rowSpacing", 8);
        engine.rootContext()->setContextProperty("_colSpacing", 12);
        engine.rootContext()->setContextProperty("_secondColumnWidth", 360);
        const QString source = qEnvironmentVariable("QGC_BLUETOOTH_SETTINGS_SOURCE", QStringLiteral(PRODUCT_QML_PATH));
        component.loadUrl(QUrl::fromLocalFile(source));
        object.reset(component.create());
        QCoreApplication::processEvents();
    }
    QObject *control(const char *name) const
    {
        return object ? object->findChild<QObject *>(QString::fromLatin1(name)) : nullptr;
    }
};

static QObject *buttonWithText(QObject *page, const QString &text)
{
    if (page->property("text").toString() == text && page->metaObject()->indexOfSignal("clicked()") >= 0) return page;
    if (auto *item = qobject_cast<QQuickItem *>(page)) {
        for (QQuickItem *child : item->childItems()) {
            if (QObject *found = buttonWithText(child, text)) return found;
        }
    }
    return nullptr;
}

static QQuickItem *visibleText(QObject *page, const QString &text)
{
    if (auto *item = qobject_cast<QQuickItem *>(page)) {
        if (item->isVisible() && item->property("text").toString() == text) return item;
        for (QQuickItem *child : item->childItems()) {
            if (QQuickItem *found = visibleText(child, text)) return found;
        }
    }
    return nullptr;
}

static bool activate(QObject *combo, int index)
{
    if (!combo || !combo->setProperty("currentIndex", index)) return false;
    const bool invoked = QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, index));
    QCoreApplication::processEvents();
    return invoked;
}

static bool edit(QObject *field, const QString &text)
{
    if (!field || !field->setProperty("text", text)) return false;
    if (field->metaObject()->indexOfSignal("textEdited()") >= 0) QMetaObject::invokeMethod(field, "textEdited");
    QCoreApplication::processEvents();
    return true;
}

static QString canonicalUuid(QString uuid)
{
    uuid = uuid.trimmed().toLower();
    if (uuid.size() == 4) uuid.prepend(QStringLiteral("0000"));
    if (uuid.size() == 8) uuid.append(QStringLiteral("-0000-1000-8000-00805f9b34fb"));
    return uuid;
}

class BluetoothSettingsTest : public QObject
{
    Q_OBJECT
private slots:
    void discoveryFailureFeedback_data()
    {
        QTest::addColumn<bool>("lowEnergy");
        QTest::addColumn<QString>("message");
        QTest::newRow("SPP-unsupported") << false << QStringLiteral("Bluetooth capability is unavailable on this device (801)");
        QTest::newRow("BLE-unsupported") << true << QStringLiteral("BLE capability is unavailable (801)");
    }

    void discoveryFailureFeedback()
    {
        QFETCH(bool, lowEnergy);
        QFETCH(QString, message);
        SettingsPage page(lowEnergy);
        QVERIFY2(page.object, qPrintable(page.component.errorString()));
        QCOMPARE(page.configuration.scanCalls, 0);
        const QString service = page.configuration.service;
        const QString write = page.configuration.write;
        const QString notify = page.configuration.notify;
        QObject *scan = buttonWithText(page.object.get(), QStringLiteral("Scan"));
        QObject *stop = buttonWithText(page.object.get(), QStringLiteral("Stop"));
        QVERIFY(scan);
        QVERIFY(stop);
        QVERIFY(QMetaObject::invokeMethod(scan, "clicked"));
        page.configuration.failDiscovery(message);
        QCoreApplication::processEvents();
        QQuickItem *feedback = visibleText(page.object.get(), message);
        QVERIFY2(feedback, "The actual backend error must be visible to the user after discovery fails");
        QCOMPARE(feedback->property("text").toString(), message);
        QCOMPARE(feedback->property("textFormat").toInt(), int(Qt::PlainText));
        QVERIFY(scan->property("enabled").toBool());
        QVERIFY(!stop->property("enabled").toBool());
        QVERIFY(page.object->property("settingsValid").toBool());

        QVERIFY(QMetaObject::invokeMethod(scan, "clicked"));
        QCoreApplication::processEvents();
        QVERIFY(!feedback->isVisible());
        QVERIFY(!visibleText(page.object.get(), message));
        QVERIFY(stop->property("enabled").toBool());
        const QString permissionError = QStringLiteral("Bluetooth access was denied (201)");
        page.configuration.failDiscovery(permissionError);
        QCoreApplication::processEvents();
        QVERIFY(visibleText(page.object.get(), permissionError));

        QVERIFY(QMetaObject::invokeMethod(scan, "clicked"));
        QVERIFY(QMetaObject::invokeMethod(stop, "clicked"));
        QCoreApplication::processEvents();
        QVERIFY(scan->property("enabled").toBool());
        QVERIFY(!visibleText(page.object.get(), permissionError));
        QCOMPARE(page.configuration.service, service);
        QCOMPARE(page.configuration.write, write);
        QCOMPARE(page.configuration.notify, notify);
    }

    void lowEnergyControlsExist()
    {
        SettingsPage page;
        QVERIFY2(page.object, qPrintable(page.component.errorString()));
        for (const char *name : {"bluetoothTransport", "bleProfile", "bleServiceUuid", "bleWriteUuid", "bleNotifyUuid"}) {
            QVERIFY2(page.control(name), qPrintable(QStringLiteral("Missing BLE settings control: %1").arg(QString::fromLatin1(name))));
        }
        QVERIFY(page.object->property("settingsValid").isValid());
    }

    void transportRoutesTheConfiguration()
    {
        SettingsPage page;
        QVERIFY2(page.object, qPrintable(page.component.errorString()));
        QObject *transport = page.control("bluetoothTransport");
        QVERIFY(transport);
        QCOMPARE(transport->property("currentIndex").toInt(), 0);
        QVERIFY(!page.configuration.lowEnergy);
        QVERIFY(activate(transport, 1));
        QVERIFY(page.configuration.lowEnergy);
        QVERIFY(activate(transport, 0));
        QVERIFY(!page.configuration.lowEnergy);

        SettingsPage initiallyBle(true);
        QVERIFY2(initiallyBle.object, qPrintable(initiallyBle.component.errorString()));
        QVERIFY(initiallyBle.control("bluetoothTransport"));
        QCOMPARE(initiallyBle.control("bluetoothTransport")->property("currentIndex").toInt(), 1);
    }

    void presets_data()
    {
        QTest::addColumn<int>("profile");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("Nordic UART") << 0 << QStringList{
            QStringLiteral("6e400001-b5a3-f393-e0a9-e50e24dcca9e"),
            QStringLiteral("6e400002-b5a3-f393-e0a9-e50e24dcca9e"),
            QStringLiteral("6e400003-b5a3-f393-e0a9-e50e24dcca9e")};
        QTest::newRow("HM-10") << 1 << QStringList{
            QStringLiteral("0000ffe0-0000-1000-8000-00805f9b34fb"),
            QStringLiteral("0000ffe1-0000-1000-8000-00805f9b34fb"),
            QStringLiteral("0000ffe1-0000-1000-8000-00805f9b34fb")};
    }

    void presets()
    {
        QFETCH(int, profile);
        QFETCH(QStringList, expected);
        SettingsPage page(true);
        QVERIFY2(page.object, qPrintable(page.component.errorString()));
        QObject *profiles = page.control("bleProfile");
        QVERIFY(profiles);
        QVERIFY(activate(profiles, 2));
        const char *fields[] = {"bleServiceUuid", "bleWriteUuid", "bleNotifyUuid"};
        for (const char *field : fields) QVERIFY(edit(page.control(field), QStringLiteral("1234")));
        QVERIFY(activate(profiles, profile));
        for (int i = 0; i < 3; ++i) {
            QVERIFY(page.control(fields[i]));
            QCOMPARE(canonicalUuid(page.control(fields[i])->property("text").toString()), expected[i]);
            QVERIFY(page.control(fields[i])->property("readOnly").toBool());
        }
        QVERIFY(page.object->property("settingsValid").toBool());
        QVERIFY(QMetaObject::invokeMethod(page.object.get(), "saveSettings"));
        QCOMPARE(canonicalUuid(page.configuration.service), expected[0]);
        QCOMPARE(canonicalUuid(page.configuration.write), expected[1]);
        QCOMPARE(canonicalUuid(page.configuration.notify), expected[2]);
    }

    void customEditsArePreservedAndSavedTogether()
    {
        SettingsPage page(true);
        QVERIFY2(page.object, qPrintable(page.component.errorString()));
        QObject *profiles = page.control("bleProfile");
        QVERIFY(profiles);
        QVERIFY(activate(profiles, 2));
        QCOMPARE(profiles->property("currentIndex").toInt(), 2);
        const char *fields[] = {"bleServiceUuid", "bleWriteUuid", "bleNotifyUuid"};
        const QStringList values{QStringLiteral(" aBcD "), QStringLiteral("1234ABCD"),
            QStringLiteral("AABBCCDD-EEFF-0011-2233-445566778899")};
        for (int i = 0; i < 3; ++i) {
            QVERIFY(page.control(fields[i]));
            QVERIFY(page.control(fields[i])->property("enabled").toBool());
            QVERIFY(!page.control(fields[i])->property("readOnly").toBool());
            QVERIFY(edit(page.control(fields[i]), values[i]));
            QCOMPARE(profiles->property("currentIndex").toInt(), 2);
        }
        QVERIFY(activate(profiles, 2));
        for (int i = 0; i < 3; ++i) QCOMPARE(page.control(fields[i])->property("text").toString(), values[i]);
        QVERIFY(page.object->property("settingsValid").toBool());
        QVERIFY(QMetaObject::invokeMethod(page.object.get(), "saveSettings"));
        QCOMPARE(page.configuration.service, values[0].trimmed());
        QCOMPARE(page.configuration.write, values[1].trimmed());
        QCOMPARE(page.configuration.notify, values[2].trimmed());
    }

    void uuidValidation_data()
    {
        QTest::addColumn<QString>("value");
        QTest::addColumn<bool>("valid");
        QTest::newRow("16-bit") << QStringLiteral("fFe0") << true;
        QTest::newRow("32-bit") << QStringLiteral("1234AbCd") << true;
        QTest::newRow("128-bit") << QStringLiteral("AABBCCDD-EEFF-0011-2233-445566778899") << true;
        QTest::newRow("trimmed-input") << QStringLiteral(" fFe0 ") << true;
        QTest::newRow("empty") << QString() << false;
        QTest::newRow("too-short") << QStringLiteral("123") << false;
        QTest::newRow("five-digits") << QStringLiteral("12345") << false;
        QTest::newRow("non-hex") << QStringLiteral("abcdefgh") << false;
        QTest::newRow("bad-long-hex") << QStringLiteral("00112233-4455-6677-8899-aabbccddeefg") << false;
        QTest::newRow("no-128-bit-separators") << QStringLiteral("00112233445566778899aabbccddeeff") << false;
        QTest::newRow("braces") << QStringLiteral("{00112233-4455-6677-8899-aabbccddeeff}") << false;
        QTest::newRow("hex-prefix") << QStringLiteral("0xFFE0") << false;
    }

    void uuidValidation()
    {
        QFETCH(QString, value);
        QFETCH(bool, valid);
        SettingsPage page(true);
        QVERIFY2(page.object, qPrintable(page.component.errorString()));
        QVERIFY(page.control("bleProfile"));
        QVERIFY(activate(page.control("bleProfile"), 2));
        const char *fields[] = {"bleServiceUuid", "bleWriteUuid", "bleNotifyUuid"};
        const QStringList original{page.configuration.service, page.configuration.write, page.configuration.notify};
        for (int tested = 0; tested < 3; ++tested) {
            for (int i = 0; i < 3; ++i) QVERIFY(edit(page.control(fields[i]), original[i]));
            QObject *field = page.control(fields[tested]);
            QVERIFY(edit(field, value));
            QCOMPARE(field->property("acceptableInput").toBool(), valid);
            QCOMPARE(page.object->property("settingsValid").toBool(), valid);
            if (!valid) {
                QVERIFY(QMetaObject::invokeMethod(page.object.get(), "saveSettings"));
                QCOMPARE(page.configuration.service, original[0]);
                QCOMPARE(page.configuration.write, original[1]);
                QCOMPARE(page.configuration.notify, original[2]);
            }
        }
    }

    void sppValidityIgnoresHiddenBleInput()
    {
        SettingsPage page;
        QVERIFY2(page.object, qPrintable(page.component.errorString()));
        QVERIFY(page.control("bleServiceUuid"));
        QVERIFY(edit(page.control("bleServiceUuid"), QStringLiteral("invalid")));
        QVERIFY(!page.control("bleServiceUuid")->property("acceptableInput").toBool());
        QVERIFY(page.object->property("settingsValid").toBool());
    }

    void unsupportedPlatformsHideLowEnergyControls()
    {
        SettingsPage page(false, false);
        QVERIFY2(page.object, qPrintable(page.component.errorString()));
        for (const char *name : {"bluetoothTransport", "bleProfile", "bleServiceUuid", "bleWriteUuid", "bleNotifyUuid"}) {
            if (QObject *control = page.control(name)) QVERIFY2(!control->property("visible").toBool(), name);
        }
        QVERIFY(buttonWithText(page.object.get(), QStringLiteral("Scan")));
        QVERIFY(buttonWithText(page.object.get(), QStringLiteral("Radio B")));
    }

    void outerSaveButtonGate_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<int>("pageKind");
        QTest::addColumn<bool>("expected");
        QTest::newRow("null-page") << QStringLiteral("Radio") << 0 << true;
        QTest::newRow("null-page-empty-name") << QString() << 0 << false;
        QTest::newRow("legacy-page") << QStringLiteral("Radio") << 1 << true;
        QTest::newRow("legacy-page-empty-name") << QString() << 1 << false;
        QTest::newRow("valid-page") << QStringLiteral("Radio") << 2 << true;
        QTest::newRow("valid-page-empty-name") << QString() << 2 << false;
        QTest::newRow("invalid-page") << QStringLiteral("Radio") << 3 << false;
        QTest::newRow("invalid-page-empty-name") << QString() << 3 << false;
    }

    void outerSaveButtonGate()
    {
        QFETCH(QString, name);
        QFETCH(int, pageKind);
        QFETCH(bool, expected);
        QFile source(QStringLiteral(PRODUCT_LINK_QML_PATH));
        QVERIFY(source.open(QIODevice::ReadOnly));
        const QRegularExpression bindingPattern(QStringLiteral("acceptButtonEnabled:\\s*([\\s\\S]*?)(?=\\n\\s*property\\s)"));
        auto matches = bindingPattern.globalMatch(QString::fromUtf8(source.readAll()));
        QVERIFY(matches.hasNext());
        const QString binding = matches.next().captured(1).trimmed();
        QVERIFY(!matches.hasNext());
        QQmlEngine engine;
        std::unique_ptr<QQmlPropertyMap> nameField(QQmlPropertyMap::create());
        nameField->insert(QStringLiteral("text"), name);
        QObject legacy;
        SettingsValidityFixture validated;
        validated.valid = pageKind == 2;
        QObject *page = pageKind == 0 ? nullptr : pageKind == 1 ? &legacy : &validated;
        std::unique_ptr<QQmlPropertyMap> loader(QQmlPropertyMap::create());
        loader->insert(QStringLiteral("item"), QVariant::fromValue(page));
        engine.rootContext()->setContextProperty("nameField", nameField.get());
        engine.rootContext()->setContextProperty("linkSettingsLoader", loader.get());
        QQmlExpression expression(engine.rootContext(), nullptr, binding);
        const QVariant result = expression.evaluate();
        QVERIFY2(!expression.hasError(), qPrintable(expression.error().toString()));
        QCOMPARE(result.toBool(), expected);
    }

    void classicScanAndDeviceSelection_data()
    {
        QTest::addColumn<bool>("supported");
        QTest::newRow("Harmony") << true;
        QTest::newRow("legacy-platform") << false;
    }

    void classicScanAndDeviceSelection()
    {
        QFETCH(bool, supported);
        SettingsPage page(false, supported);
        QVERIFY2(page.object, qPrintable(page.component.errorString()));
        QObject *scan = buttonWithText(page.object.get(), QStringLiteral("Scan"));
        QObject *stop = buttonWithText(page.object.get(), QStringLiteral("Stop"));
        QObject *device = buttonWithText(page.object.get(), QStringLiteral("Radio B"));
        QVERIFY(scan);
        QVERIFY(stop);
        QVERIFY(device);
        QVERIFY(scan->property("enabled").toBool());
        QVERIFY(!stop->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(scan, "clicked"));
        QCOMPARE(page.configuration.scanCalls, 1);
        QVERIFY(page.configuration.scanning);
        QVERIFY(!scan->property("enabled").toBool());
        QVERIFY(stop->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(device, "clicked"));
        QCOMPARE(page.configuration.device, QStringLiteral("Radio B"));
        QVERIFY(device->property("checked").toBool());
        QVERIFY(QMetaObject::invokeMethod(stop, "clicked"));
        QCOMPARE(page.configuration.stopCalls, 1);
        QVERIFY(!page.configuration.scanning);
    }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    qmlRegisterModule("QGroundControl", 1, 0);
    qmlRegisterModule("QGroundControl.Palette", 1, 0);
    qmlRegisterModule("QGroundControl.Controls", 1, 0);
    for (const char *name : {"QGCButton", "QGCLabel", "QGCComboBox", "QGCTextField"}) {
        qmlRegisterType(QUrl::fromLocalFile(QStringLiteral(TEST_BINARY_DIR "/") + QString::fromLatin1(name) + ".qml"),
                        "QGroundControl.Controls", 1, 0, name);
    }
    qmlRegisterSingletonType(QUrl::fromLocalFile(QStringLiteral(TEST_BINARY_DIR "/ScreenTools.qml")),
                             "QGroundControl.ScreenTools", 1, 0, "ScreenTools");
    BluetoothSettingsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "BluetoothSettingsTest.moc"

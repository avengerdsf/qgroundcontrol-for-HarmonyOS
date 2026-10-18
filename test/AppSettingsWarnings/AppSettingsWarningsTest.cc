#include <QtCore/QFile>
#include <QtCore/QTextStream>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQml/QQmlExpression>
#include <memory>

static std::unique_ptr<QObject> create(QQmlEngine& engine, const QByteArray& source)
{
    QQmlComponent component(&engine);
    component.setData(source, QUrl());
    std::unique_ptr<QObject> object(component.create());
    if (!object)
        qCritical() << component.errors();
    return object;
}

static QObject* button(QObject* page, const QString& text)
{
    for (QObject* child : page->findChildren<QObject*>()) {
        if (child->property("text").toString() == text && child->metaObject()->indexOfSignal("clicked()") >= 0)
            return child;
    }
    return nullptr;
}

static QObject* propertyObject(QObject* page, const char* property)
{
    for (QObject* child : page->findChildren<QObject*>()) {
        if (child->metaObject()->indexOfProperty(property) >= 0)
            return child;
    }
    return nullptr;
}

int main(int argc, char** argv)
{
    QGuiApplication application(argc, argv);
    const auto url = [](const char* name) {
        return QUrl::fromLocalFile(QStringLiteral(TEST_BINARY_DIR "/") + QString::fromLatin1(name) + ".qml");
    };
    for (const char* name :
         {"QGCButton", "QGCLabel", "QGCComboBox", "QGCCheckBox", "QGCCheckBoxSlider", "QGCTextField", "QGCTextArea",
          "QGCFlickable", "QGCListView", "SettingsGroupLayout", "LabelledComboBox", "MessageDialog"}) {
        qmlRegisterType(url(name), "QGroundControl.Controls", 1, 0, name);
    }
    qmlRegisterType(url("QGCPalette"), "QGroundControl", 1, 0, "QGCPalette");
    qmlRegisterSingletonType(url("ScreenTools"), "QGroundControl.Controls", 1, 0, "ScreenTools");
    qmlRegisterSingletonType(url("QGroundControl"), "QGroundControl", 1, 0, "QGroundControl");
    QQmlEngine engine;
    QStringList failures;
    QList<QQmlError> warnings;
    QObject::connect(&engine, &QQmlEngine::warnings, &engine,
                     [&](const QList<QQmlError>& errors) { warnings.append(errors); });
    const auto check = [&](bool value, const QString& message) {
        if (!value)
            failures.append(message);
    };
    const auto checkWarnings = [&]() {
        QCoreApplication::processEvents();
        for (const auto& warning : warnings)
            failures.append(warning.toString());
        warnings.clear();
    };
    const auto evaluate = [&](QObject* page, const QString& script) {
        QQmlExpression expression(qmlContext(page), page, script);
        expression.evaluate();
        check(!expression.hasError(), expression.error().toString());
    };
    auto* qgc = engine.singletonInstance<QObject*>(qmlTypeId("QGroundControl", 1, 0, "QGroundControl"));
    auto* vehicles = qgc->property("multiVehicleManager").value<QObject*>();
    const auto load = [&](const char* name) {
        QQmlComponent component(&engine, url(name));
        std::unique_ptr<QObject> page(component.createWithInitialProperties({{"width", 1200}, {"height", 800}}));
        check(bool(page), component.errorString());
        return page;
    };
    auto control = load("PX4LogControl");
    auto upload = load("PX4LogUploadSettings");
    auto files = load("PX4LogFileManager");
    if (!control || !upload || !files) {
        QFile report(QStringLiteral(TEST_BINARY_DIR "/result.txt"));
        if (!report.open(QIODevice::WriteOnly | QIODevice::Text))
            return 1;
        QTextStream(&report) << failures.join('\n') << '\n';
        return 1;
    }
    checkWarnings();
    QObject* start = button(control.get(), QStringLiteral("Start Logging"));
    QObject* stop = button(control.get(), QStringLiteral("Stop Logging"));
    check(start && stop, QStringLiteral("Production log controls must remain available"));
    if (!start || !stop)
        return 1;
    check(!start->property("enabled").toBool() && !stop->property("enabled").toBool(),
          QStringLiteral("Offline log controls must be disabled"));
    evaluate(upload.get(), QStringLiteral("saveItems()"));
    checkWarnings();

    auto manager = create(engine, R"(import QtQml
QtObject {
    property bool logRunning: false
    property bool canStartLog: true
    property bool enableAutoStart: false
    property string emailAddress: "pilot@example.test"
    property string description: "Test flight"
    property string uploadURL: "https://example.test/upload"
    property string videoURL: ""
    property int windSpeed: 8
    property string rating: "good"
    property string feedback: ""
    property bool publicLog: false
    property bool enableAutoUpload: false
    property bool deleteAfterUpload: false
    property var logFiles: []
    property bool uploading: false
    property int confirmations: 0
    signal selectedCountChanged()
    function startLogging() { logRunning = true }
    function stopLogging() { logRunning = false }
    function deleteLog() { confirmations++ }
    function uploadLog() { confirmations++ }
    function cancelUpload() { confirmations++ }
})");
    auto vehicle = create(engine,
                          "import QtQml\nQtObject { property bool px4Firmware: true; property bool apmFirmware: false; "
                          "property var mavlinkLogManager: null }");
    if (!manager || !vehicle)
        return 1;
    vehicle->setProperty("mavlinkLogManager", QVariant::fromValue(manager.get()));
    QObject* wind = propertyObject(upload.get(), "_windValues");
    QObject* rating = propertyObject(upload.get(), "_ratingValues");
    check(wind && rating, QStringLiteral("Production wind and rating controls must remain available"));
    if (!wind || !rating)
        return 1;
    const QString confirm = QStringLiteral(
        "deleteDialog.buttonClicked(16384, 0); uploadDialog.buttonClicked(16384, 0); cancelDialog.buttonClicked(16384, "
        "0)");
    for (int cycle = 0; cycle < 3; ++cycle) {
        manager->setProperty("windSpeed", 8);
        manager->setProperty("rating", QStringLiteral("good"));
        vehicles->setProperty("activeVehicle", QVariant::fromValue(vehicle.get()));
        checkWarnings();
        check(wind->property("currentIndex").toInt() == 3 && rating->property("currentIndex").toInt() == 4,
              QStringLiteral("Connected manager must populate wind and rating"));
        manager->setProperty("windSpeed", 10);
        manager->setProperty("rating", QStringLiteral("great"));
        checkWarnings();
        check(wind->property("currentIndex").toInt() == 4 && rating->property("currentIndex").toInt() == 5,
              QStringLiteral("Backend changes must update wind and rating"));
        check(start->property("enabled").toBool(), QStringLiteral("Connected vehicle must enable starting logs"));
        QMetaObject::invokeMethod(wind, "activated", Q_ARG(int, 1));
        QMetaObject::invokeMethod(rating, "activated", Q_ARG(int, 5));
        check(manager->property("windSpeed").toInt() == 0 &&
                  manager->property("rating").toString() == QStringLiteral("great"),
              QStringLiteral("User selections must reach the manager"));
        evaluate(
            upload.get(),
            QStringLiteral("emailField.text = 'updated@example.test'; descField.text = 'Updated flight'; saveItems()"));
        check(manager->property("emailAddress").toString() == QStringLiteral("updated@example.test") &&
                  manager->property("description").toString() == QStringLiteral("Updated flight"),
              QStringLiteral("Form saving must retain field values"));
        evaluate(files.get(), confirm);
        check(manager->property("confirmations").toInt() == (cycle + 1) * 3,
              QStringLiteral("Connected confirmation callbacks must remain functional"));
        QMetaObject::invokeMethod(start, "clicked");
        check(manager->property("logRunning").toBool(), QStringLiteral("Start action must reach the manager"));
        check(stop->property("enabled").toBool(), QStringLiteral("Running log must enable stop"));
        QMetaObject::invokeMethod(stop, "clicked");
        check(!manager->property("logRunning").toBool(), QStringLiteral("Stop action must reach the manager"));
        vehicles->setProperty("activeVehicle", QVariant::fromValue<QObject*>(nullptr));
        checkWarnings();
        check(!start->property("enabled").toBool(), QStringLiteral("Disconnect must disable start"));
        evaluate(files.get(), confirm);
        evaluate(
            upload.get(),
            QStringLiteral("emailField.text = 'offline@example.test'; descField.text = 'Offline edit'; saveItems()"));
        checkWarnings();
        check(manager->property("emailAddress").toString() == QStringLiteral("updated@example.test") &&
                  manager->property("description").toString() == QStringLiteral("Updated flight"),
              QStringLiteral("Disconnected edits must not write back to the previous manager"));
        check(manager->property("confirmations").toInt() == (cycle + 1) * 3,
              QStringLiteral("Pending confirmations must not act after disconnect"));
    }
    vehicles->setProperty("activeVehicle", QVariant::fromValue(vehicle.get()));
    manager.reset();
    checkWarnings();
    vehicle.reset();
    checkWarnings();
    files.reset();
    upload.reset();
    control.reset();
    checkWarnings();
    QFile report(QStringLiteral(TEST_BINARY_DIR "/result.txt"));
    if (!report.open(QIODevice::WriteOnly | QIODevice::Text))
        return 1;
    QTextStream(&report) << (failures.isEmpty()
                                 ? QStringLiteral("PASS: PX4 logging bindings, edits, actions and disconnect lifecycle")
                                 : failures.join('\n'))
                         << '\n';
    return failures.isEmpty() ? 0 : 1;
}

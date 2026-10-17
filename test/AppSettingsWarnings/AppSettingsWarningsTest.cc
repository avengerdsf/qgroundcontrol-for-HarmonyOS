#include <QtCore/QLoggingCategory>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlExpression>
#include <cstdio>
#include <memory>

static std::unique_ptr<QObject> create(QQmlEngine &engine, const QByteArray &source)
{
    QQmlComponent component(&engine);
    component.setData(source, QUrl());
    std::unique_ptr<QObject> object(component.create());
    if (!object) qCritical() << component.errors();
    return object;
}

static QObject *button(QObject *page, const QString &text)
{
    for (QObject *child : page->findChildren<QObject *>()) {
        if (child->property("text").toString() == text && child->metaObject()->indexOfSignal("clicked()") >= 0) return child;
    }
    return nullptr;
}

int main(int argc, char **argv)
{
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message) {
        std::fprintf(stderr, "%s\n", message.toUtf8().constData());
    });
    QGuiApplication application(argc, argv);
    QLoggingCategory::setFilterRules(QStringLiteral("qt.qml.connections=false"));
    const auto url = [](const char *name) {
        return QUrl::fromLocalFile(QStringLiteral(TEST_BINARY_DIR "/") + QString::fromLatin1(name) + ".qml");
    };
    for (const char *module : {"QGroundControl.FactControls", "QGroundControl.MultiVehicleManager"}) qmlRegisterModule(module, 1, 0);
    qmlRegisterType(url("Fact"), "QGroundControl.FactSystem", 1, 0, "Fact");
    qmlRegisterType(url("QGCPalette"), "QGroundControl.Palette", 1, 0, "QGCPalette");
    for (const char *name : {"QGCButton", "QGCLabel", "QGCComboBox", "QGCCheckBox", "QGCTextField", "QGCTextArea", "QGCFlickable", "QGCListView"}) {
        qmlRegisterType(url(name), "QGroundControl.Controls", 1, 0, name);
    }
    qmlRegisterSingletonType(url("ScreenTools"), "QGroundControl.ScreenTools", 1, 0, "ScreenTools");
    qmlRegisterSingletonType(url("QGroundControl"), "QGroundControl", 1, 0, "QGroundControl");
    QQmlEngine engine;
    QList<QQmlError> warnings;
    QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&](const QList<QQmlError> &errors) { warnings.append(errors); });
    auto *qgc = engine.singletonInstance<QObject *>(qmlTypeId("QGroundControl", 1, 0, "QGroundControl"));
    auto *vehicles = qgc->property("multiVehicleManager").value<QObject *>();
    QQmlComponent pageComponent(&engine, QUrl::fromLocalFile(QStringLiteral(PRODUCT_QML_PATH)));
    std::unique_ptr<QObject> page(pageComponent.create());
    if (!page) { qCritical() << pageComponent.errors(); return 1; }
    page->setProperty("width", 1200);
    page->setProperty("height", 800);
    bool passed = true;
    const auto check = [&](bool value, const char *message) { if (!value) { qCritical() << message; passed = false; } };
    const auto checkWarnings = [&]() {
        QCoreApplication::processEvents();
        if (!warnings.isEmpty()) { qCritical() << warnings; passed = false; warnings.clear(); }
    };
    const auto evaluate = [&](const QString &script) {
        QQmlExpression expression(qmlContext(page.get()), page.get(), script);
        expression.evaluate();
        check(!expression.hasError(), qPrintable(expression.error().toString()));
    };
    checkWarnings();
    QObject *start = button(page.get(), QStringLiteral("Start Logging"));
    QObject *stop = button(page.get(), QStringLiteral("Stop Logging"));
    check(start && stop, "Original log controls must remain available");
    if (!start || !stop) return 1;
    check(!start->property("enabled").toBool() && !stop->property("enabled").toBool(), "Offline log controls must be disabled");

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
    property var logFiles: null
    property bool uploading: false
    property int confirmations: 0
    signal selectedCountChanged()
    function startLogging() { logRunning = true }
    function stopLogging() { logRunning = false }
    function deleteLog() { confirmations++ }
    function uploadLog() { confirmations++ }
    function cancelUpload() { confirmations++ }
})");
    if (!manager) return 1;
    auto vehicle = create(engine, "import QtQml\nQtObject { property bool px4Firmware: true; property bool apmFirmware: false; property var mavlinkLogManager: null }");
    if (!vehicle) return 1;
    vehicle->setProperty("mavlinkLogManager", QVariant::fromValue(manager.get()));
    for (int cycle = 0; cycle < 3; ++cycle) {
        manager->setProperty("windSpeed", 8);
        manager->setProperty("rating", QStringLiteral("good"));
        vehicles->setProperty("activeVehicle", QVariant::fromValue(vehicle.get()));
        checkWarnings();
        for (QObject *control : page->findChildren<QObject *>()) {
            if (control->property("textRole").toString() != QStringLiteral("text")) continue;
            const bool wind = control->property("count").toInt() == 5;
            check(control->property("currentIndex").toInt() == (wind ? 3 : 4), "Connected manager must populate wind and rating");
            manager->setProperty(wind ? "windSpeed" : "rating", wind ? QVariant(10) : QVariant(QStringLiteral("great")));
            checkWarnings();
            check(control->property("currentIndex").toInt() == (wind ? 4 : 5), "Backend changes must update the combo selection");
        }
        check(start->property("enabled").toBool(), "Connected vehicle must enable starting logs");
        evaluate(QStringLiteral("windCombo.activated(1); ratingCombo.activated(5)"));
        check(manager->property("windSpeed").toInt() == 0 && manager->property("rating").toString() == QStringLiteral("great"), "User selections must reach the manager");
        evaluate(QStringLiteral("emailField.text = 'updated@example.test'; descField.text = 'Updated flight'; saveItems()"));
        check(manager->property("emailAddress").toString() == QStringLiteral("updated@example.test") && manager->property("description").toString() == QStringLiteral("Updated flight"), "Form saving must retain field values");
        const QString confirm = QStringLiteral("deleteDialog.buttonClicked(16384, 0); uploadDialog.buttonClicked(16384, 0); cancelDialog.buttonClicked(16384, 0)");
        evaluate(confirm);
        check(manager->property("confirmations").toInt() == (cycle + 1) * 3, "Connected confirmation callbacks must remain functional");
        check(QMetaObject::invokeMethod(start, "clicked"), "Start action must remain callable");
        check(manager->property("logRunning").toBool(), "Start action must reach the real manager binding");
        check(stop->property("enabled").toBool(), "Running log must enable stop");
        QMetaObject::invokeMethod(stop, "clicked");
        check(!manager->property("logRunning").toBool(), "Stop action must reach the manager");
        vehicles->setProperty("activeVehicle", QVariant::fromValue<QObject *>(nullptr));
        checkWarnings();
        check(!start->property("enabled").toBool(), "Disconnect must disable start");
        evaluate(confirm);
        evaluate(QStringLiteral("saveItems()"));
        check(manager->property("confirmations").toInt() == (cycle + 1) * 3, "Pending confirmations must not act after disconnect");
    }
    vehicles->setProperty("activeVehicle", QVariant::fromValue(vehicle.get()));
    manager.reset();
    checkWarnings();
    vehicle.reset();
    checkWarnings();
    page.reset();
    checkWarnings();
    return passed ? 0 : 1;
}

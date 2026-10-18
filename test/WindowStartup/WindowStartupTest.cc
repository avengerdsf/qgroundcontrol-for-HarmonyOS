#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QSettings>
#include <QtCore/QThread>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtQml/QQmlIncubator>
#include <QtQuick/QQuickWindow>
#include <cstdio>
#include <memory>

struct FirstVisible
{
    bool seen = false;
    QRect geometry;
    QWindow::Visibility visibility = QWindow::Hidden;
    Qt::WindowState state = Qt::WindowNoState;
};

class WindowIncubator : public QQmlIncubator
{
public:
    WindowIncubator() : QQmlIncubator(QQmlIncubator::Synchronous) {}

    FirstVisible first;

protected:
    void setInitialState(QObject* object) override
    {
        auto* window = qobject_cast<QQuickWindow*>(object);
        QObject::connect(window, &QWindow::visibleChanged, window, [this, window](bool visible) {
            if (visible && !first.seen) {
                first.seen = true;
                first.geometry = window->geometry();
                first.state = window->windowState();
            }
        });
        QObject::connect(window, &QWindow::visibilityChanged, window, [this](QWindow::Visibility visibility) {
            if (visibility != QWindow::Hidden && first.visibility == QWindow::Hidden) {
                first.visibility = visibility;
            }
        });
    }
};

static void processEvents(int milliseconds)
{
    for (int elapsed = 0; elapsed < milliseconds; elapsed += 10) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    QCoreApplication::processEvents();
}

int main(int argc, char** argv)
{
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& message) {
        const QByteArray utf8 = message.toUtf8();
        std::fprintf(stderr, "%s\n", utf8.constData());
    });
    qputenv("QML_DISABLE_DISK_CACHE", "1");
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("QGCWindowStartupTest");
    QCoreApplication::setApplicationName("QGCWindowStartupTest");
    const QString settingsPath = QStringLiteral(TEST_BINARY_DIR "/settings");
    QDir().mkpath(settingsPath);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsPath);

    const auto sourceUrl = [](const char* file) {
        return QUrl::fromLocalFile(QStringLiteral(TEST_SOURCE_DIR "/") + QString::fromLatin1(file));
    };
    qmlRegisterSingletonType(sourceUrl("QGroundControl.qml"), "QGroundControl", 1, 0, "QGroundControl");
    qmlRegisterSingletonType(sourceUrl("ScreenTools.qml"), "QGroundControl.Controls", 1, 0, "ScreenTools");
    qmlRegisterType(sourceUrl("../../src/MainWindow/MainWindowSavedState.qml"), "QGroundControl.Controls", 1, 0,
                    "MainWindowSavedState");
    for (const char* module :
         {"QGroundControl.Palette", "QGroundControl.FactControls", "QGroundControl.FlyView", "QGroundControl.FlightMap",
          "QGroundControl.PlanView", "QGroundControl.Toolbar", "QGroundControl.UTMSP"}) {
        qmlRegisterModule(module, 1, 0);
    }

    const auto screenToolsType = qmlTypeId("QGroundControl.Controls", 1, 0, "ScreenTools");
    const auto groundControlType = qmlTypeId("QGroundControl", 1, 0, "QGroundControl");
    bool failed = false;
    const QString selectedCase = qEnvironmentVariable("QGC_WINDOW_STARTUP_CASE");
    int executedCases = 0;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) {
            qCritical() << message;
            failed = true;
        }
    };
    const auto run = [&](const char* name, bool saved, QWindow::Visibility state, bool mobile, bool fakeMobile,
                         bool saveEnabled, bool harmonyTablet = false) {
        if (!selectedCase.isEmpty() && selectedCase != QString::fromLatin1(name)) {
            return;
        }
        ++executedCases;
        QSettings settings;
        settings.clear();
        if (saved) {
            settings.beginGroup("MainWindowState");
            settings.setValue("x", 70);
            settings.setValue("y", 80);
            settings.setValue("width", 730);
            settings.setValue("height", 510);
            // A fresh INI reader returns numeric values as strings, unlike
            // QSettings instances sharing an in-process typed value cache.
            settings.setValue("visibility",
                              harmonyTablet ? QVariant(QString::number(int(state))) : QVariant(int(state)));
            settings.endGroup();
        }
        settings.sync();

        QQmlEngine engine;
        QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&](const QList<QQmlError>& warnings) {
            qCritical() << name << warnings;
            failed = true;
        });
        QObject* screenTools = engine.singletonInstance<QObject*>(screenToolsType);
        screenTools->setProperty("isMobile", mobile);
        screenTools->setProperty("isHarmonyTablet", harmonyTablet);
        screenTools->setProperty("isFakeMobile", fakeMobile);
        QObject* groundControl = engine.singletonInstance<QObject*>(groundControlType);
        QObject* plugin = groundControl->property("corePlugin").value<QObject*>();
        plugin->property("options").value<QObject*>()->setProperty("enableSaveMainWindowPosition", saveEnabled);

        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(TEST_BINARY_DIR "/MainWindowStartup.qml")));
        WindowIncubator incubator;
        component.create(incubator);
        if (incubator.isError()) {
            qCritical() << name << incubator.errors();
            failed = true;
            return;
        }
        std::unique_ptr<QQuickWindow> window(qobject_cast<QQuickWindow*>(incubator.object()));
        if (!window) {
            qCritical() << name << "did not create a window" << component.errors();
            failed = true;
            return;
        }
        processEvents(50);
        qInfo() << name << "first visible" << incubator.first.geometry << incubator.first.visibility << "state"
                << incubator.first.state << "settled" << window->geometry() << window->visibility();
        check(incubator.first.seen, "Main window was never shown");
        qInfo() << name << "bottom safe-area padding" << window->property("bottomPadding");
        check(window->property("bottomPadding").toReal() == 0,
              "Application content must retain the upstream bottom safe-area override");
        check(incubator.first.visibility == window->visibility(), "Main window state changed after first visibility");
        if (!mobile && (!harmonyTablet || (saved && saveEnabled)) &&
            (!saved || !saveEnabled || fakeMobile || state == QWindow::Windowed)) {
            check(incubator.first.geometry.size() == window->size(), "Main window size changed after first visibility");
        }

        if (fakeMobile) {
            check(incubator.first.geometry.size() == QSize(731, 411),
                  "Fake mobile size was not prepared before showing");
            check(window->size() == QSize(731, 411), "Fake mobile size was not applied");
            check(window->visibility() == QWindow::Windowed, "Fake mobile should remain windowed");
        } else if (mobile) {
            check(window->visibility() == QWindow::FullScreen, "Mobile window was not fullscreen");
            check(incubator.first.state == Qt::WindowFullScreen, "Mobile window was shown before setting fullscreen");
        } else if (harmonyTablet && (!saved || !saveEnabled)) {
            check(window->visibility() == QWindow::Maximized, "Harmony tablet default must retain the system caption");
            check(incubator.first.state == Qt::WindowMaximized,
                  "Harmony tablet must be maximized before first visibility");
        } else if (saved && saveEnabled) {
            const auto restoredState = harmonyTablet && state == QWindow::FullScreen ? QWindow::Maximized
                                       : state == QWindow::Maximized || state == QWindow::FullScreen
                                           ? state
                                           : QWindow::Windowed;
            check(window->visibility() == restoredState, "Saved window state was not restored");
            if (restoredState == QWindow::Windowed) {
                check(window->geometry() == QRect(70, 80, 730, 510), "Saved geometry was not restored");
                check(incubator.first.geometry == QRect(70, 80, 730, 510),
                      "Saved geometry was not prepared before showing");
            } else {
                check(incubator.first.geometry == QRect(70, 80, 730, 510),
                      "Saved normal geometry was not prepared before showing");
                check(incubator.first.state ==
                          (restoredState == QWindow::Maximized ? Qt::WindowMaximized : Qt::WindowFullScreen),
                      "Saved window state was not prepared before showing");
            }
        } else {
            check(window->width() >= 500 && window->height() >= 250, "Desktop default size is too small");
            check(window->visibility() == QWindow::Windowed, "New desktop window should be windowed");
        }

        // Keep existing persistence for a resized window, and preserve its normal
        // geometry when switching to maximized or fullscreen states.
        if (!mobile && !fakeMobile && saveEnabled && state == QWindow::Windowed && (!harmonyTablet || saved)) {
            window->setGeometry(90, 100, 710, 520);
            processEvents(1300);
            QSettings persisted;
            check(persisted.value("MainWindowState/width").toInt() == 710 &&
                      persisted.value("MainWindowState/height").toInt() == 520,
                  "Resized window geometry was not saved");
            window->setVisibility(QWindow::Maximized);
            processEvents(1300);
            persisted.sync();
            check(persisted.value("MainWindowState/visibility").toInt() == QWindow::Maximized,
                  "Maximized state was not saved");
            check(persisted.value("MainWindowState/width").toInt() == 710 &&
                      persisted.value("MainWindowState/height").toInt() == 520,
                  "Maximizing overwrote saved normal geometry");
            window->setVisibility(QWindow::FullScreen);
            processEvents(1300);
            persisted.sync();
            check(persisted.value("MainWindowState/visibility").toInt() == QWindow::FullScreen,
                  "Fullscreen state was not saved");
            check(persisted.value("MainWindowState/width").toInt() == 710 &&
                      persisted.value("MainWindowState/height").toInt() == 520,
                  "Fullscreen overwrote saved normal geometry");
        } else if (mobile || fakeMobile || !saveEnabled) {
            processEvents(1300);
            QSettings persisted;
            check(persisted.value("MainWindowState/x").toInt() == 70 &&
                      persisted.value("MainWindowState/y").toInt() == 80 &&
                      persisted.value("MainWindowState/width").toInt() == 730 &&
                      persisted.value("MainWindowState/height").toInt() == 510 &&
                      persisted.value("MainWindowState/visibility").toInt() == QWindow::Windowed,
                  "Disabled window persistence changed saved desktop settings");
        }
        if (harmonyTablet && !fakeMobile && saveEnabled) {
            // Keep the upstream geometry debounce. Once the floating rectangle
            // is saved, entering fullscreen and closing need no extra delay.
            window->setVisibility(QWindow::Windowed);
            window->setGeometry(135, 145, 700, 490);
            bool normalGeometrySaved = false;
            for (int elapsed = 0; elapsed < 1500 && !normalGeometrySaved; elapsed += 20) {
                processEvents(20);
                QSettings normal;
                normal.sync();
                normalGeometrySaved = normal.value("MainWindowState/x").toInt() == 135 &&
                                      normal.value("MainWindowState/y").toInt() == 145 &&
                                      normal.value("MainWindowState/width").toInt() == 700 &&
                                      normal.value("MainWindowState/height").toInt() == 490;
            }
            check(normalGeometrySaved, "Stable floating rectangle did not reach the settings store");
            window->setVisibility(QWindow::FullScreen);
            window->close();
            QSettings persisted;
            check(persisted.value("MainWindowState/visibility").toInt() == QWindow::FullScreen,
                  "Closing immediately did not flush the final fullscreen state");
            QFile savedIni(persisted.fileName());
            check(savedIni.open(QIODevice::ReadOnly), "Saved window INI could not be read from disk");
            check(savedIni.readAll().replace("\r\n", "\n").contains("\nvisibility=5\n"),
                  "Fullscreen state was not serialized as the upstream numeric INI value");
            check(persisted.value("MainWindowState/x").toInt() == 135 &&
                      persisted.value("MainWindowState/y").toInt() == 145 &&
                      persisted.value("MainWindowState/width").toInt() == 700 &&
                      persisted.value("MainWindowState/height").toInt() == 490,
                  "Closing fullscreen lost the most recent windowed rectangle");

            window->setVisibility(QWindow::Windowed);
            window->setGeometry(155, 165, 720, 500);
            window->close();
            persisted.sync();
            check(persisted.value("MainWindowState/visibility").toInt() == QWindow::Windowed &&
                      persisted.value("MainWindowState/x").toInt() == 155 &&
                      persisted.value("MainWindowState/y").toInt() == 165 &&
                      persisted.value("MainWindowState/width").toInt() == 720 &&
                      persisted.value("MainWindowState/height").toInt() == 500,
                  "Closing immediately did not flush the final windowed state and rectangle");
        }
        window->hide();
    };

    run("saved-windowed", true, QWindow::Windowed, false, false, true);
    run("first-desktop-start", false, QWindow::Windowed, false, false, true);
    run("saving-disabled", true, QWindow::Windowed, false, false, false);
    run("saved-maximized", true, QWindow::Maximized, false, false, true);
    run("saved-fullscreen", true, QWindow::FullScreen, false, false, true);
    run("mobile", true, QWindow::Windowed, true, false, true);
    run("fake-mobile", true, QWindow::Windowed, true, true, true);
    run("harmony-tablet-first-start", false, QWindow::Windowed, false, false, true, true);
    run("harmony-tablet-saved-windowed", true, QWindow::Windowed, false, false, true, true);
    run("harmony-tablet-saved-maximized", true, QWindow::Maximized, false, false, true, true);
    run("harmony-tablet-saved-fullscreen", true, QWindow::FullScreen, false, false, true, true);
    run("harmony-tablet-saved-hidden-fallback", true, QWindow::Hidden, false, false, true, true);
    run("harmony-tablet-saved-minimized-fallback", true, QWindow::Minimized, false, false, true, true);
    run("harmony-tablet-saving-disabled", true, QWindow::Windowed, false, false, false, true);
    run("harmony-2in1-saved-windowed", true, QWindow::Windowed, false, false, true);
    check(executedCases > 0, "No window startup scenario matched the requested case");
    qInfo() << "Startup scenarios executed:" << executedCases;
    QSettings().clear();
    QDir(settingsPath).removeRecursively();
    return failed ? 1 : 0;
}

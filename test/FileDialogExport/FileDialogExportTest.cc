#include <QtCore/QAbstractListModel>
#include <QtCore/QDir>
#include <QtCore/QRegularExpression>
#include <QtCore/qscopeguard.h>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQml/QQmlExpression>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <memory>

#include "LogEntry.h"
#include "QGCFileDialogController.h"

class MessageModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int filterLevel MEMBER filterLevel)
    Q_PROPERTY(QStringList categoriesList MEMBER categoriesList CONSTANT)
    Q_PROPERTY(QString filterCategory MEMBER filterCategory)
    Q_PROPERTY(bool filterRegex MEMBER filterRegex NOTIFY filterChanged)
public:
    int filterLevel = -1;
    QStringList categoriesList;
    QString filterCategory;
    bool filterRegex = false;

    Q_INVOKABLE void clear() {}

    Q_INVOKABLE void setFilterTextDeferred(const QString&) {}

    int rowCount(const QModelIndex& = {}) const override { return 0; }

    QVariant data(const QModelIndex&, int) const override { return {}; }

    QString lastFile;
signals:
    void filterChanged();
};

class LogManagerFixture : public QObject
{
    Q_OBJECT
    Q_PROPERTY(MessageModel* model READ model CONSTANT)
    Q_PROPERTY(bool hasError READ hasError CONSTANT)
public:
    MessageModel messages;

    MessageModel* model() { return &messages; }

    bool hasError() const { return false; }

    Q_INVOKABLE void writeMessages(const QString& file) { messages.lastFile = file; }

    Q_INVOKABLE void clearError() {}
};

static QVariant evaluate(QObject* object, const QString& script)
{
    QQmlExpression expression(qmlContext(object), object, script);
    const QVariant value = expression.evaluate();
    if (expression.hasError()) {
        qFatal("%s", qPrintable(expression.error().toString()));
    }
    return value;
}

static QObject* idObject(QObject* object, const char* id)
{
    return evaluate(object, QString::fromLatin1(id)).value<QObject*>();
}

static QObject* childWithProperty(QObject* object, const char* name)
{
    for (QObject* child : object->findChildren<QObject*>()) {
        if (child->metaObject()->indexOfProperty(name) >= 0)
            return child;
    }
    return nullptr;
}

class FileDialogExportTest : public QObject
{
    Q_OBJECT
private slots:

    void saveDefault_data()
    {
        QTest::addColumn<bool>("mobile");
        QTest::addColumn<int>("format");
        QTest::newRow("native-txt") << false << 0;
        QTest::newRow("native-csv") << false << 1;
        QTest::newRow("mobile-txt") << true << 0;
        QTest::newRow("mobile-csv") << true << 1;
    }

    void saveDefault()
    {
        QFETCH(bool, mobile);
        QFETCH(int, format);
        const QString folder = QStringLiteral(TEST_BINARY_DIR "/日志 空格");
        const auto cleanupFolder = qScopeGuard([&] { QDir().rmdir(folder); });
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(1000, 700);
        LogManagerFixture* logger =
            engine.singletonInstance<LogManagerFixture*>(qmlTypeId("QGroundControl.LogManager", 1, 0, "LogManager"));
        MessageModel& messages = logger->messages;
        engine.rootContext()->setContextProperty("mainWindow", window.contentItem());
        QObject* logSettings = nullptr;
        const int qgcType = qmlTypeId("QGroundControl", 1, 0, "QGroundControl");
        QObject* qgc = engine.singletonInstance<QObject*>(qgcType);
        QObject* options = qgc->property("corePlugin").value<QObject*>()->property("options").value<QObject*>();
        options->setProperty("useMobileFileDialog", mobile);
        QVERIFY(QDir().mkpath(folder));
        logSettings =
            qgc->property("settingsManager").value<QObject*>()->property("logManagerSettings").value<QObject*>();
        logSettings->property("saveFormat").value<QObject*>()->setProperty("rawValue", format);
        QObject* settings =
            qgc->property("settingsManager").value<QObject*>()->property("appSettings").value<QObject*>();
        settings->setProperty("logSavePath", folder);

        QQmlComponent paletteComponent(&engine, QUrl::fromLocalFile(QStringLiteral(TEST_BINARY_DIR "/QGCPalette.qml")));
        std::unique_ptr<QObject> palette(paletteComponent.create());
        QVERIFY2(palette, qPrintable(paletteComponent.errorString()));
        engine.rootContext()->setContextProperty("qgcPal", palette.get());
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(TEST_BINARY_DIR "/AppLogging.qml")));
        std::unique_ptr<QObject> page(component.create());
        QVERIFY2(page, qPrintable(component.errorString()));
        auto* pageItem = qobject_cast<QQuickItem*>(page.get());
        pageItem->setParentItem(window.contentItem());
        pageItem->setSize(window.size());
        QObject* writeDialog = idObject(page.get(), "saveFileDialog");
        QObject* writeButton = nullptr;
        for (QObject* child : page->findChildren<QObject*>()) {
            if (child->property("text").toString() == QStringLiteral("Save") &&
                child->metaObject()->indexOfSignal("clicked()") >= 0)
                writeButton = child;
        }
        QVERIFY(writeButton);
        QObject* fullDialog = childWithProperty(writeDialog, "selectedFile");
        QVERIFY(fullDialog);
        // Exercise the desktop FileDialog API without an operating-system modal.
        evaluate(fullDialog, "options = FileDialog.DontUseNativeDialog");

        for (int attempt = 0; attempt != 2; ++attempt) {
            if (attempt == 1)
                writeDialog->setProperty("defaultFileName", QStringLiteral("stale-default-for-previous-open.txt"));
            QVERIFY(QMetaObject::invokeMethod(writeButton, "clicked"));
            const QString name = writeDialog->property("defaultFileName").toString();
            QVERIFY2(name != QStringLiteral("stale-default-for-previous-open.txt"),
                     "Reopening Save must refresh the default file name");
            const QString suffix = format == 0 ? QStringLiteral("txt") : QStringLiteral("csv");
            QVERIFY2(QRegularExpression("^QGCConsole-\\d{4}-\\d{2}-\\d{2}_\\d{2}-\\d{2}-\\d{2}\\." + suffix + "$")
                         .match(name)
                         .hasMatch(),
                     qPrintable("Missing timestamped default: " + name));
            if (mobile) {
                QObject* popup = nullptr;
                for (QObject* child : writeDialog->findChildren<QObject*>()) {
                    if (child->property("preventClose").isValid())
                        popup = child;
                }
                QVERIFY(popup);
                QObject* field = childWithProperty(popup, "echoMode");
                QVERIFY(field);
                QCOMPARE(field->property("text").toString(), name);
                if (attempt == 1)
                    field->setProperty("text", QStringLiteral("用户自定义 %#.") +
                                                   (format == 0 ? QStringLiteral("txt") : QStringLiteral("csv")));
                QVERIFY(QMetaObject::invokeMethod(popup, "accepted"));
                const QString expected = attempt == 0
                                             ? name
                                             : QStringLiteral("用户自定义 %#.") +
                                                   (format == 0 ? QStringLiteral("txt") : QStringLiteral("csv"));
                QCOMPARE(messages.lastFile, folder + "/" + expected);
                QVERIFY(!popup->property("preventClose").toBool());
                delete popup;
            } else {
                const QUrl selected = fullDialog->property("selectedFile").toUrl();
                QCOMPARE(selected.toLocalFile(), folder + "/" + name);
                if (attempt == 1)
                    fullDialog->setProperty("selectedFile",
                                            QUrl::fromLocalFile(folder + QStringLiteral("/用户自定义 %#.txt")));
                QVERIFY(QMetaObject::invokeMethod(fullDialog, "accepted"));
                QCOMPARE(messages.lastFile,
                         attempt == 0 ? selected.toLocalFile() : folder + QStringLiteral("/用户自定义 %#.txt"));
                QVERIFY(QMetaObject::invokeMethod(writeDialog, "close"));
            }
        }

        QQmlComponent genericComponent(&engine,
                                       QUrl::fromLocalFile(QStringLiteral(TEST_BINARY_DIR "/QGCFileDialog.qml")));
        std::unique_ptr<QObject> generic(genericComponent.createWithInitialProperties(
            {{"folder", folder}, {"nameFilters", QStringList{"Log files (*.txt)"}}}));
        QVERIFY2(generic, qPrintable(genericComponent.errorString()));
        QCOMPARE(generic->property("defaultFileName").toString(), QString());
        if (mobile) {
            QVERIFY(QMetaObject::invokeMethod(generic.get(), "openForSave"));
            QObject* popup = nullptr;
            for (QObject* child : generic->findChildren<QObject*>()) {
                if (child->property("preventClose").isValid())
                    popup = child;
            }
            QVERIFY(popup);
            QObject* field = childWithProperty(popup, "echoMode");
            QVERIFY(field);
            QCOMPARE(field->property("text").toString(), QString());
            QSignalSpy saved(generic.get(), SIGNAL(acceptedForSave(QString)));
            QVERIFY(QMetaObject::invokeMethod(popup, "accepted"));
            QCOMPARE(saved.count(), 0);
            QVERIFY(popup->property("preventClose").toBool());
            delete popup;
        } else {
            QObject* genericFullDialog = childWithProperty(generic.get(), "selectedFile");
            evaluate(genericFullDialog, "options = FileDialog.DontUseNativeDialog");
            QVERIFY(QMetaObject::invokeMethod(generic.get(), "openForSave"));
            QVERIFY(genericFullDialog->property("selectedFile").toUrl().isEmpty());
            QVERIFY(QMetaObject::invokeMethod(generic.get(), "close"));
            const QString specialName = QStringLiteral("默认文件 %#.txt");
            generic->setProperty("defaultFileName", specialName);
            QVERIFY(QMetaObject::invokeMethod(generic.get(), "openForSave"));
            QCOMPARE(genericFullDialog->property("selectedFile").toUrl().toLocalFile(), folder + "/" + specialName);
            QVERIFY(QMetaObject::invokeMethod(generic.get(), "close"));
        }
    }
};

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    const auto fixture = [](const char* name) {
        return QUrl::fromLocalFile(QStringLiteral(TEST_SOURCE_DIR "/") + name + ".qml");
    };
    const auto control = [](const char* name) {
        return QUrl::fromLocalFile(QStringLiteral(TEST_BINARY_DIR "/") + name + ".qml");
    };
    qmlRegisterSingletonType(fixture("QGroundControl"), "QGroundControl", 1, 0, "QGroundControl");
    qmlRegisterSingletonType(fixture("ScreenTools"), "QGroundControl.Controls", 1, 0, "ScreenTools");
    qmlRegisterType(control("QGCPalette"), "QGroundControl", 1, 0, "QGCPalette");
    qmlRegisterSingletonType<QGCFileDialogController>(
        "QGroundControl", 1, 0, "QGCFileDialogController",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return new QGCFileDialogController; });
    qmlRegisterSingletonType<LogManagerFixture>(
        "QGroundControl.LogManager", 1, 0, "LogManager",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return new LogManagerFixture; });
    qmlRegisterUncreatableMetaObject(LogEntry::staticMetaObject, "QGroundControl.LogManager", 1, 0, "LogEntry",
                                     "Enums");
    qmlRegisterType(control("LoggingCategoriesDialog"), "QGroundControl.LogManager", 1, 0, "LoggingCategoriesDialog");
    for (const char* name : {"QGCButton", "QGCLabel", "QGCComboBox", "QGCTextField", "QGCFlickable", "QGCMouseArea"}) {
        qmlRegisterType(control(name), "QGroundControl.Controls", 1, 0, name);
    }
    qmlRegisterModuleImport("QGroundControl.Controls", 1, "QtQuick.Controls");
    FileDialogExportTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "FileDialogExportTest.moc"

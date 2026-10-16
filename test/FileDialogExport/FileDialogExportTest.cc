#include "QGCFileDialogController.h"

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

class MessageModel : public QAbstractListModel
{
    Q_OBJECT
public:
    int rowCount(const QModelIndex & = {}) const override { return 0; }
    QVariant data(const QModelIndex &, int) const override { return {}; }
    Q_INVOKABLE void writeMessages(const QString &file) { lastFile = file; }
    QString lastFile;
signals:
    void writeStarted();
    void writeFinished();
};

static QVariant evaluate(QObject *object, const QString &script)
{
    QQmlExpression expression(qmlContext(object), object, script);
    const QVariant value = expression.evaluate();
    if (expression.hasError()) {
        qFatal("%s", qPrintable(expression.error().toString()));
    }
    return value;
}

static QObject *idObject(QObject *object, const char *id)
{
    return evaluate(object, QString::fromLatin1(id)).value<QObject *>();
}

static QObject *childWithProperty(QObject *object, const char *name)
{
    for (QObject *child : object->findChildren<QObject *>()) {
        if (child->metaObject()->indexOfProperty(name) >= 0) return child;
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
        QTest::newRow("native") << false;
        QTest::newRow("mobile") << true;
    }

    void saveDefault()
    {
        QFETCH(bool, mobile);
        const QString folder = QStringLiteral(TEST_BINARY_DIR "/日志 空格");
        const auto cleanupFolder = qScopeGuard([&] { QDir().rmdir(folder); });
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(1000, 700);
        MessageModel messages;
        engine.rootContext()->setContextProperty("mainWindow", window.contentItem());
        engine.rootContext()->setContextProperty("debugMessageModel", &messages);
        const int qgcType = qmlTypeId("QGroundControl", 1, 0, "QGroundControl");
        QObject *qgc = engine.singletonInstance<QObject *>(qgcType);
        QObject *options = qgc->property("corePlugin").value<QObject *>()->property("options").value<QObject *>();
        options->setProperty("useMobileFileDialog", mobile);
        QVERIFY(QDir().mkpath(folder));
        QObject *settings = qgc->property("settingsManager").value<QObject *>()->property("appSettings").value<QObject *>();
        settings->setProperty("logSavePath", folder);

        QQmlComponent paletteComponent(&engine, QUrl::fromLocalFile(QStringLiteral(TEST_BINARY_DIR "/QGCPalette.qml")));
        std::unique_ptr<QObject> palette(paletteComponent.create());
        QVERIFY2(palette, qPrintable(paletteComponent.errorString()));
        engine.rootContext()->setContextProperty("qgcPal", palette.get());
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(TEST_BINARY_DIR "/AppMessages.qml")));
        // Existing AppMessages Connections syntax warns on Qt 6.12.
        const QRegularExpression connectionsWarning(".*QML Connections: Implicitly defined onFoo properties.*");
        QTest::ignoreMessage(QtWarningMsg, connectionsWarning);
        QTest::ignoreMessage(QtWarningMsg, connectionsWarning);
        std::unique_ptr<QObject> page(component.create());
        QVERIFY2(page, qPrintable(component.errorString()));
        auto *pageItem = qobject_cast<QQuickItem *>(page.get());
        pageItem->setParentItem(window.contentItem());
        pageItem->setSize(window.size());
        QObject *writeDialog = idObject(page.get(), "writeDialog");
        QObject *writeButton = idObject(page.get(), "writeButton");
        QObject *fullDialog = childWithProperty(writeDialog, "selectedFile");
        QVERIFY(fullDialog);
        // Exercise the desktop FileDialog API without an operating-system modal.
        evaluate(fullDialog, "options = FileDialog.DontUseNativeDialog");

        QString previous;
        for (int attempt = 0; attempt != 2; ++attempt) {
            QVERIFY(QMetaObject::invokeMethod(writeButton, "clicked"));
            const QString name = writeDialog->property("defaultFileName").toString();
            QVERIFY2(QRegularExpression("^QGCConsole-\\d{4}-\\d{2}-\\d{2}-\\d{2}-\\d{2}-\\d{2}-\\d{3}\\.txt$").match(name).hasMatch(),
                     qPrintable("Missing timestamped default: " + name));
            QVERIFY(name != previous);
            previous = name;
            if (mobile) {
                QObject *popup = nullptr;
                for (QObject *child : window.contentItem()->children()) {
                    if (child->property("preventClose").isValid()) popup = child;
                }
                QVERIFY(popup);
                QObject *field = childWithProperty(popup, "echoMode");
                QVERIFY(field);
                QCOMPARE(field->property("text").toString(), name);
                if (attempt == 1) field->setProperty("text", QStringLiteral("用户自定义 %#.txt"));
                QVERIFY(QMetaObject::invokeMethod(popup, "accepted"));
                const QString expected = attempt == 0 ? name : QStringLiteral("用户自定义 %#.txt");
                QCOMPARE(messages.lastFile, folder + "/" + expected);
                QVERIFY(!popup->property("preventClose").toBool());
                delete popup;
            } else {
                const QUrl selected = fullDialog->property("selectedFile").toUrl();
                QCOMPARE(selected.toLocalFile(), folder + "/" + name);
                if (attempt == 1) fullDialog->setProperty("selectedFile", QUrl::fromLocalFile(folder + QStringLiteral("/用户自定义 %#.txt")));
                QVERIFY(QMetaObject::invokeMethod(fullDialog, "accepted"));
                QCOMPARE(messages.lastFile, attempt == 0 ? selected.toLocalFile() : folder + QStringLiteral("/用户自定义 %#.txt"));
                QVERIFY(QMetaObject::invokeMethod(writeDialog, "close"));
            }
            QTest::qWait(5);
        }

        QQmlComponent genericComponent(&engine, QUrl::fromLocalFile(QStringLiteral(TEST_BINARY_DIR "/QGCFileDialog.qml")));
        std::unique_ptr<QObject> generic(genericComponent.createWithInitialProperties({{"folder", folder}, {"nameFilters", QStringList{"Log files (*.txt)"}}}));
        QVERIFY2(generic, qPrintable(genericComponent.errorString()));
        QCOMPARE(generic->property("defaultFileName").toString(), QString());
        if (mobile) {
            QVERIFY(QMetaObject::invokeMethod(generic.get(), "openForSave"));
            QObject *popup = nullptr;
            for (QObject *child : window.contentItem()->children()) {
                if (child->property("preventClose").isValid()) popup = child;
            }
            QVERIFY(popup);
            QObject *field = childWithProperty(popup, "echoMode");
            QVERIFY(field);
            QCOMPARE(field->property("text").toString(), QString());
            QSignalSpy saved(generic.get(), SIGNAL(acceptedForSave(QString)));
            QVERIFY(QMetaObject::invokeMethod(popup, "accepted"));
            QCOMPARE(saved.count(), 0);
            QVERIFY(popup->property("preventClose").toBool());
            delete popup;
        } else {
            QObject *genericFullDialog = childWithProperty(generic.get(), "selectedFile");
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

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    const auto fixture = [](const char *name) { return QUrl::fromLocalFile(QStringLiteral(TEST_SOURCE_DIR "/") + name + ".qml"); };
    const auto control = [](const char *name) { return QUrl::fromLocalFile(QStringLiteral(TEST_BINARY_DIR "/") + name + ".qml"); };
    qmlRegisterSingletonType(fixture("QGroundControl"), "QGroundControl", 1, 0, "QGroundControl");
    qmlRegisterSingletonType(fixture("ScreenTools"), "QGroundControl.ScreenTools", 1, 0, "ScreenTools");
    qmlRegisterType(control("QGCPalette"), "QGroundControl.Palette", 1, 0, "QGCPalette");
    qmlRegisterType<QGCFileDialogController>("QGroundControl.Controllers", 1, 0, "QGCFileDialogController");
    qmlRegisterType(control("FactComboBox"), "QGroundControl.FactControls", 1, 0, "FactComboBox");
    qmlRegisterModule("QGroundControl.FactSystem", 1, 0);
    qmlRegisterModule("QGroundControl.Controls", 1, 0);
    FileDialogExportTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "FileDialogExportTest.moc"

#include <QtCore/QCoreApplication>
#include <QtCore/QLibraryInfo>
#include <QtCore/QLocale>
#include <QtCore/QLoggingCategory>
#include <QtCore/QStringList>
#include <QtCore/QTranslator>
#include <QtGui/QFontDatabase>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlComponent>
#include <cstdio>
#include <memory>

Q_LOGGING_CATEGORY(QGCApplicationLog, "qgc.application")

class AppSettings
{
public:
    static QLocale::Language selected;

    static QLocale::Language _qLocaleLanguageEarlyAccess() { return selected; }
};

QLocale::Language AppSettings::selected = QLocale::AnyLanguage;

class JsonParsing
{
public:
    static QTranslator* translator()
    {
        static QTranslator instance;
        return &instance;
    }
};

// Compile the production function without starting the application or writing settings.
class QGCApplication : public QCoreApplication
{
public:
    using QCoreApplication::QCoreApplication;
    void setLanguage();

    void languageChanged(const QLocale) {}

    QQmlApplicationEngine* _qmlAppEngine = nullptr;
    QLocale _locale;
    QTranslator _qgcTranslatorSourceCode;
    QTranslator _qgcTranslatorQtLibs;
};

#include "SetLanguage.inc"

int main(int argc, char** argv)
{
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& message) {
        const QByteArray utf8 = message.toUtf8();
        std::fprintf(stderr, "%s\n", utf8.constData());
    });

    QGCApplication app(argc, argv);
    QQmlApplicationEngine engine;
    app._qmlAppEngine = &engine;
    QQmlComponent component(&engine);
    component.setData("import QtQml\nQtObject { property string label: qsTr(\"Cancel\") }",
                      QUrl("file:///APMSensorsComponent.qml"));
    std::unique_ptr<QObject> label(component.create());
    if (!label) {
        qCritical() << component.errors();
        return 2;
    }

    bool failed = false;
    const auto check = [&](QLocale::Language language, const QLocale& expectedLocale, const QString& expectedLabel) {
        AppSettings::selected = language;
        app.setLanguage();
        QCoreApplication::processEvents();
        const QString actualLabel = label->property("label").toString();
        qInfo() << "Selected" << language << "current" << app._locale.name() << "default" << QLocale().name() << "label"
                << actualLabel;
        if (app._locale != expectedLocale || QLocale() != expectedLocale ||
            (!expectedLabel.isNull() && actualLabel != expectedLabel)) {
            qCritical() << "Language switch failed; expected locale" << expectedLocale.name() << "and label"
                        << expectedLabel;
            failed = true;
        }
        const QString jsonTranslation = JsonParsing::translator()->translate(
            "App.SettingsGroup.json", "Never,Always,When in Follow Me Flight Mode");
        if ((language == QLocale::English && !jsonTranslation.isNull()) ||
            (language == QLocale::Chinese &&
             jsonTranslation != QString::fromUtf8("从不,总是,处于“跟随我”飞行模式时"))) {
            qCritical() << "JSON metadata translator kept the previous language:" << jsonTranslation;
            failed = true;
        }
        const QString fenceTranslation =
            JsonParsing::translator()->translate("APM-MavCmdInfoCommon.json", "Enable,Disable");
        const QString fenceEnumStrings =
            fenceTranslation.isEmpty() ? QStringLiteral("Enable,Disable") : fenceTranslation;
        const QStringList fenceLabels = fenceEnumStrings.split(',');
        if ((language == QLocale::Chinese &&
             fenceLabels != QStringList{QString::fromUtf8("启用"), QString::fromUtf8("禁用")}) ||
            (language == QLocale::English && fenceEnumStrings != QStringLiteral("Enable,Disable"))) {
            qCritical() << "Fence enable enum labels must match values 1,0 in order:" << fenceLabels;
            failed = true;
        }
    };
    check(QLocale::Chinese, QLocale("zh_CN"), QString::fromUtf8("取消"));
    check(QLocale::English, QLocale("en_US"), "Cancel");
    check(QLocale::Chinese, QLocale("zh_CN"), QString::fromUtf8("取消"));
    check(QLocale::AnyLanguage, QLocale::system(), QString());
    return failed ? 1 : 0;
}

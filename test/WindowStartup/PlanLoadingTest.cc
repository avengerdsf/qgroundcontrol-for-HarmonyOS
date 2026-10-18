#include <QtCore/QDebug>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <cstdio>
#include <memory>

static QList<QObject*> plans(QObject* root)
{
    QList<QObject*> result;
    for (QObject* child : root->findChildren<QObject*>()) {
        if (child->property("isPlanFixture").toBool())
            result.append(child);
    }
    return result;
}

int main(int argc, char** argv)
{
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& message) {
        std::fprintf(stderr, "%s\n", message.toUtf8().constData());
    });
    QGuiApplication application(argc, argv);
    qmlRegisterSingletonType(QUrl::fromLocalFile(QStringLiteral(TEST_SOURCE_DIR "/QGroundControl.qml")),
                             "QGroundControl", 1, 0, "QGroundControl");
    qmlRegisterSingletonType(QUrl::fromLocalFile(QStringLiteral(TEST_SOURCE_DIR "/ScreenTools.qml")),
                             "QGroundControl.Controls", 1, 0, "ScreenTools");
    qmlRegisterType(QUrl::fromLocalFile(QStringLiteral(TEST_SOURCE_DIR "/PlanFixture.qml")), "PlanFixtures", 1, 0,
                    "PlanView");
    qmlRegisterType(QUrl::fromLocalFile(QStringLiteral(TEST_SOURCE_DIR "/PlanFlyFixture.qml")), "PlanFixtures", 1, 0,
                    "FlyView");
    bool passed = true;
    auto check = [&](bool value, const char* message) {
        if (!value) {
            qCritical() << message;
            passed = false;
        }
    };
    for (bool harmony : {true, false}) {
        QQmlEngine engine;
        QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&](const QList<QQmlError>& errors) {
            qCritical() << errors;
            passed = false;
        });
        auto* screenTools =
            engine.singletonInstance<QObject*>(qmlTypeId("QGroundControl.Controls", 1, 0, "ScreenTools"));
        screenTools->setProperty("isHarmonyTablet", harmony);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(TEST_BINARY_DIR "/PlanLoading.qml")));
        std::unique_ptr<QObject> root(component.create());
        if (!root) {
            qCritical() << component.errors();
            return 1;
        }
        check(plans(root.get()).size() == (harmony ? 0 : 1),
              "Harmony startup must not construct the unused plan; desktop remains eager");
        QVariant allowed;
        QMetaObject::invokeMethod(root.get(), "checkForUnsavedMission", Q_RETURN_ARG(QVariant, allowed));
        check(allowed.toBool(), "Close without any plan edits must remain allowed");
        QMetaObject::invokeMethod(root.get(), "showFlyView");
        check(plans(root.get()).size() == (harmony ? 0 : 1), "Showing flight view must not construct a deferred plan");
        QMetaObject::invokeMethod(root.get(), "showPlanView");
        const auto planItems = plans(root.get());
        check(planItems.size() == 1, "First plan selection must construct exactly one plan");
        if (planItems.size() != 1) {
            return 1;
        }
        QObject* plan = planItems.first();
        auto* controller = plan->property("_planMasterController").value<QObject*>();
        controller->setProperty("dirty", true);
        QMetaObject::invokeMethod(root.get(), "showFlyView");
        QMetaObject::invokeMethod(root.get(), "checkForUnsavedMission", Q_RETURN_ARG(QVariant, allowed));
        check(!allowed.toBool() && root->property("confirmations").toInt() == 1,
              "A retained dirty plan must still require confirmation");
        QMetaObject::invokeMethod(root.get(), "showPlanView");
        check(plans(root.get()).size() == 1 && plans(root.get()).first() == plan &&
                  controller->property("dirty").toBool(),
              "Switching views must retain the same plan and its edits");
    }
    return passed ? 0 : 1;
}

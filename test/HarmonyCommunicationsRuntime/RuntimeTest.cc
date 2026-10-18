#include <QtCore/QElapsedTimer>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QTimer>
#include <QtCore/private/qcore_ohos_p.h>
#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <cstdio>
#include <cstring>

#include "HarmonyBluetoothBridge.h"

#ifdef QGC_UNITTEST_BUILD
#error The runtime probe must use NativeBluetoothProvider.
#endif
#ifndef Q_OS_HARMONY
#error The runtime probe requires HarmonyOS.
#endif

static void marker(const QString& text)
{
    std::fprintf(stdout, "%s\n", text.toUtf8().constData());
    std::fflush(stdout);
}

static bool configure(const QString& mode, const QString& scenario = QStringLiteral("normal"))
{
    return QOhosJsThreadGateway::eval(
        [mode, scenario](QOhosJsState& state) {
            try {
                state.eval<QNapi::Value>("RuntimeControl.configure(*)", {mode.toStdString(), scenario.toStdString()});
                return true;
            } catch (const Napi::Error& error) {
                marker(QStringLiteral("QGC_RUNTIME_CONTROL_ERROR %1").arg(QString::fromUtf8(error.what())));
                return false;
            }
        },
        Q_FUNC_INFO);
}

static QJsonDocument controlJson(const std::string& expression, bool context = false)
{
    const QString json = QOhosJsThreadGateway::eval(
        [expression, context](QOhosJsState& state) {
            try {
                std::vector<QNapi::ValueWrapper> arguments;
                if (context) {
                    const auto ability = state.defaultQAbility();
                    if (!ability)
                        return QStringLiteral("{\"helperError\":\"UIAbility is unavailable\"}");
                    arguments.emplace_back(ability->Get("context"));
                }
                return QString::fromStdString(state.eval<QNapi::String>(expression, arguments).Utf8Value());
            } catch (const Napi::Error& error) {
                return QString::fromUtf8(QJsonDocument(QJsonObject{{"helperError", QString::fromUtf8(error.what())}})
                                             .toJson(QJsonDocument::Compact));
            }
        },
        Q_FUNC_INFO);
    return QJsonDocument::fromJson(json.toUtf8());
}

static QJsonObject lastRequest(const QString& operation, const QString& transport = QStringLiteral("spp"))
{
    const QJsonArray requests = controlJson("RuntimeControl.requestsJson()").array();
    for (int index = requests.size() - 1; index >= 0; --index) {
        const QJsonObject request = requests[index].toObject();
        if (request.value("operation").toString() == operation && request.value("transport").toString() == transport)
            return request;
    }
    return {};
}

static quint64 sessionId(const QJsonObject& request)
{
    return static_cast<quint64>(request.value("session").toDouble());
}

static bool releaseOpen(quint64 session)
{
    return QOhosJsThreadGateway::eval(
        [session](QOhosJsState& state) {
            return state.eval<QNapi::Boolean>("RuntimeControl.releaseOpen(*)", {static_cast<double>(session)}).Value();
        },
        Q_FUNC_INFO);
}

static bool deliverRead(quint64 session, const QByteArray& data)
{
    return QOhosJsThreadGateway::eval(
        [session, data](QOhosJsState& state) {
            auto buffer = Napi::Uint8Array::New(state.env(), data.size());
            std::memcpy(buffer.Data(), data.constData(), data.size());
            return state.eval<QNapi::Boolean>("RuntimeControl.deliverRead(*)", {static_cast<double>(session), buffer})
                .Value();
        },
        Q_FUNC_INFO);
}

struct PermissionResult
{
    std::atomic_bool done = false;
    QString json;
    QString error;
};

static std::shared_ptr<PermissionResult> requestPermission()
{
    const auto result = std::make_shared<PermissionResult>();
    QOhosJsThreadGateway::invoke([result](QOhosJsState& state) {
        try {
            const auto ability = state.defaultQAbility();
            if (!ability) {
                result->error = QStringLiteral("UIAbility is unavailable");
                result->done = true;
                return;
            }
            state.evalToPromiseOrRejectOnThrow("RuntimeControl.requestPermission(*)", {ability->Get("context")})
                .onThen(
                    [result](const Napi::CallbackInfo& info) {
                        try {
                            result->json = QString::fromStdString(info[0].As<Napi::String>().Utf8Value());
                        } catch (const Napi::Error& error) {
                            result->error = QString::fromUtf8(error.what());
                        }
                        result->done = true;
                    },
                    [result](const Napi::CallbackInfo& info) {
                        result->error = QString::fromStdString(info[0].ToString().Utf8Value());
                        result->done = true;
                    });
        } catch (const Napi::Error& error) {
            result->error = QString::fromUtf8(error.what());
            result->done = true;
        }
    });
    return result;
}

class RuntimeTest : public QObject
{
    Q_OBJECT
private:
    void parametersAndBinary(bool lowEnergy)
    {
        QVERIFY(configure(QStringLiteral("controlled")));
        const QString transport = lowEnergy ? QStringLiteral("ble") : QStringLiteral("spp");
        HarmonyBluetoothOptions options;
        options.lowEnergy = lowEnergy;
        options.serviceUuid = QStringLiteral("12345678-0000-1000-8000-00805f9b34fb");
        options.writeUuid = QStringLiteral("12345679-0000-1000-8000-00805f9b34fb");
        options.notifyUuid = QStringLiteral("1234567a-0000-1000-8000-00805f9b34fb");
        HarmonyBluetoothBridge bridge;
        QSignalSpy found(&bridge, &HarmonyBluetoothBridge::deviceDiscovered);
        QSignalSpy connected(&bridge, &HarmonyBluetoothBridge::connected);
        QSignalSpy received(&bridge, &HarmonyBluetoothBridge::dataReceived);
        QSignalSpy sent(&bridge, &HarmonyBluetoothBridge::dataSent);
        bridge.startScan(options);
        QTRY_COMPARE(found.count(), 1);
        QCOMPARE(found[0][0].toString(), QStringLiteral("12:34:56:78:90:AB"));
        QCOMPARE(found[0][1].toString(), QStringLiteral("runtime-") + transport);
        QVERIFY(lastRequest(QStringLiteral("startScan"), transport).value("contextToken").toDouble() > 0);
        bridge.stopScan();
        QTRY_VERIFY(!lastRequest(QStringLiteral("stopScan"), transport).isEmpty());
        bridge.open(QStringLiteral("12:34:56:78:90:AB"), options);
        QTRY_COMPARE(connected.count(), 1);
        QVERIFY(bridge.isOpen());
        const QJsonObject opened = lastRequest(QStringLiteral("open"), transport);
        QCOMPARE(opened.value("address").toString(), QStringLiteral("12:34:56:78:90:AB"));
        QVERIFY(opened.value("contextToken").toDouble() > 0);
        if (lowEnergy) {
            QCOMPARE(opened.value("serviceUuid").toString(), options.serviceUuid);
            QCOMPARE(opened.value("writeUuid").toString(), options.writeUuid);
            QCOMPARE(opened.value("notifyUuid").toString(), options.notifyUuid);
        } else {
            QVERIFY(opened.value("serviceUuid").toString().isEmpty());
        }
        QTRY_VERIFY(!lastRequest(QStringLiteral("read"), transport).isEmpty());
        const QByteArray bytes = QByteArray::fromHex("fd00ff0102807f00112233");
        QVERIFY(deliverRead(sessionId(opened), bytes));
        QTRY_COMPARE(received.count(), 1);
        QCOMPARE(received[0][0].toByteArray(), bytes);
        bridge.write(bytes);
        QTRY_COMPARE(sent.count(), 1);
        QCOMPARE(sent[0][0].toByteArray(), bytes);
        QByteArray written;
        for (const QJsonValue& value : lastRequest(QStringLiteral("write"), transport).value("data").toArray()) {
            written.append(static_cast<char>(value.toInt()));
        }
        QCOMPARE(written, bytes);
        bridge.close();
        QVERIFY(!bridge.isOpen());
        QTRY_VERIFY(!lastRequest(QStringLiteral("close"), transport).isEmpty());
    }

    void permission(bool repeated)
    {
        const QString phase = qEnvironmentVariable("QGC_RUNTIME_PHASE");
        QVERIFY2(phase == QStringLiteral("grant") || phase == QStringLiteral("deny"),
                 "Permission requests require an explicit grant/deny phase");
        marker(QStringLiteral("QGC_RUNTIME_PERMISSION_REQUEST phase=%1 repeat=%2").arg(phase).arg(repeated ? 1 : 0));
        const auto pending = requestPermission();
        QTRY_VERIFY_WITH_TIMEOUT(pending->done.load(), 90000);
        QVERIFY2(pending->error.isEmpty(), qPrintable(pending->error));
        const QJsonObject result = QJsonDocument::fromJson(pending->json.toUtf8()).object();
        marker(QStringLiteral("QGC_RUNTIME_PERMISSION_RESULT phase=%1 repeat=%2 json=%3")
                   .arg(phase)
                   .arg(repeated ? 1 : 0)
                   .arg(pending->json));
        const QJsonArray authorization = result.value("authResults").toArray();
        QCOMPARE(authorization.size(), 1);
        const int expected = phase == QStringLiteral("grant") ? 0 : -1;
        QCOMPARE(authorization.first().toInt(), expected);
        if (repeated || qEnvironmentVariable("QGC_RUNTIME_EXPECT_DIALOG") == QStringLiteral("1")) {
            QVERIFY2(result.value("dialogReported").toBool(),
                     "SDK did not report whether the repeat request showed a dialog");
            QCOMPARE(result.value("dialogShown").toBool(), !repeated);
        }
        const QJsonObject snapshot = controlJson("RuntimeControl.capabilitySnapshot(*)", true).object();
        QVERIFY2(!snapshot.contains("helperError"), qPrintable(QString::fromUtf8(QJsonDocument(snapshot).toJson())));
        QCOMPARE(snapshot.value("permissionError").toInt(), 0);
        QCOMPARE(snapshot.value("permission").toInt(), expected);
    }

private slots:

    void factoriesReady()
    {
        QVERIFY(configure(QStringLiteral("controlled")));
        bool ready = false;
        QString error;
        QVERIFY2(HarmonyBluetoothBridge::isSupported(&error, &ready), qPrintable(error));
        QVERIFY2(ready, qPrintable(error));
    }

    void sppParametersAndBinary() { parametersAndBinary(false); }

    void bleParametersAndBinary() { parametersAndBinary(true); }

    void errorPropagation()
    {
        for (bool lowEnergy : {false, true}) {
            QVERIFY(configure(QStringLiteral("controlled"), QStringLiteral("openError")));
            HarmonyBluetoothOptions options;
            options.lowEnergy = lowEnergy;
            HarmonyBluetoothBridge bridge;
            QSignalSpy errors(&bridge, &HarmonyBluetoothBridge::errorOccurred);
            QSignalSpy disconnected(&bridge, &HarmonyBluetoothBridge::disconnected);
            bridge.open(QStringLiteral("12:34:56:78:90:AB"), options);
            QTRY_COMPARE(errors.count(), 1);
            QCOMPARE(errors[0][0].toString(), QStringLiteral("runtime-native-open-error (2900099)"));
            QCOMPARE(disconnected.count(), 1);
            QVERIFY(!bridge.isOpen());
        }
    }

    void lateCloseIsolation()
    {
        QVERIFY(configure(QStringLiteral("controlled"), QStringLiteral("lateOpen")));
        {
            HarmonyBluetoothBridge bridge;
            QSignalSpy connected(&bridge, &HarmonyBluetoothBridge::connected);
            bridge.open(QStringLiteral("12:34:56:78:90:AB"));
            QTRY_VERIFY(!lastRequest(QStringLiteral("open")).isEmpty());
            const quint64 previous = sessionId(lastRequest(QStringLiteral("open")));
            bridge.close();
            QTRY_COMPARE(sessionId(lastRequest(QStringLiteral("close"))), previous);
            QVERIFY(releaseOpen(previous));
            bridge.open(QStringLiteral("12:34:56:78:90:CD"));
            QTRY_VERIFY(sessionId(lastRequest(QStringLiteral("open"))) != previous);
            QVERIFY(releaseOpen(sessionId(lastRequest(QStringLiteral("open")))));
            QTRY_COMPARE(connected.count(), 1);
            QVERIFY(bridge.isOpen());
            bridge.close();
        }
        QVERIFY(configure(QStringLiteral("controlled"), QStringLiteral("lateRead")));
        HarmonyBluetoothBridge bridge;
        QSignalSpy connected(&bridge, &HarmonyBluetoothBridge::connected);
        QSignalSpy received(&bridge, &HarmonyBluetoothBridge::dataReceived);
        bridge.open(QStringLiteral("12:34:56:78:90:AB"));
        QTRY_COMPARE(connected.count(), 1);
        QTRY_VERIFY(!lastRequest(QStringLiteral("read")).isEmpty());
        const quint64 previous = sessionId(lastRequest(QStringLiteral("read")));
        bridge.close();
        bridge.open(QStringLiteral("12:34:56:78:90:CD"));
        QTRY_COMPARE(connected.count(), 2);
        QTRY_VERIFY(sessionId(lastRequest(QStringLiteral("read"))) != previous);
        const quint64 current = sessionId(lastRequest(QStringLiteral("read")));
        QVERIFY(deliverRead(previous, QByteArrayLiteral("old")));
        QVERIFY(deliverRead(current, QByteArrayLiteral("current")));
        QTRY_COMPARE(received.count(), 1);
        QCOMPARE(received[0][0].toByteArray(), QByteArrayLiteral("current"));
        bridge.close();
    }

    void productionSnapshot()
    {
        QVERIFY(configure(QStringLiteral("production")));
        const QJsonObject snapshot = controlJson("RuntimeControl.capabilitySnapshot(*)", true).object();
        marker(QStringLiteral("QGC_RUNTIME_PRODUCTION %1")
                   .arg(QString::fromUtf8(QJsonDocument(snapshot).toJson(QJsonDocument::Compact))));
        QVERIFY(!snapshot.contains("helperError"));
        QVERIFY(snapshot.value("contextToken").toDouble() > 0);
        QCOMPARE(snapshot.value("permissionError").toInt(), 0);
        bool ready = false;
        QString error;
        const bool supported = HarmonyBluetoothBridge::isSupported(&error, &ready);
        QVERIFY2(ready, qPrintable(error));
        QCOMPARE(supported, snapshot.value("capability").toBool());
        for (bool lowEnergy : {false, true}) {
            const int state = snapshot.value("state").toInt();
            const bool poweredOff = snapshot.value("permission").toInt() == 0 &&
                                    snapshot.value("stateError").toInt() == 0 && state != 2 &&
                                    (!lowEnergy || state != 5);
            const int expected = !supported ? 801 : poweredOff ? 2900003 : 0;
            if (!expected) {
                marker(QStringLiteral("QGC_RUNTIME_PRODUCTION transport=%1 errorPath=not-applicable no-radio-operation")
                           .arg(lowEnergy ? QStringLiteral("ble") : QStringLiteral("spp")));
                continue;
            }
            HarmonyBluetoothOptions options;
            options.lowEnergy = lowEnergy;
            HarmonyBluetoothBridge bridge;
            QSignalSpy errors(&bridge, &HarmonyBluetoothBridge::errorOccurred);
            bridge.startScan(options);
            QTRY_COMPARE(errors.count(), 1);
            QVERIFY(errors[0][0].toString().contains(QString::number(expected)));
            QVERIFY(!bridge.isScanning());
            errors.clear();
            bridge.open(QStringLiteral("12:34:56:78:90:AB"), options);
            QTRY_COMPARE(errors.count(), 1);
            QVERIFY(errors[0][0].toString().contains(QString::number(expected)));
            QVERIFY(!bridge.isOpen());
        }
    }

    void permissionRequest() { permission(false); }

    void permissionRepeat() { permission(true); }
};

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QWindow window;
    window.setTitle(QStringLiteral("QGC Runtime Tests"));
    window.resize(640, 360);
    window.show();
    window.requestActivate();
    const QString phase = qEnvironmentVariable("QGC_RUNTIME_PHASE");
    QElapsedTimer elapsed;
    elapsed.start();
    QTimer readyTimer;
    readyTimer.setInterval(100);
    QObject::connect(&readyTimer, &QTimer::timeout, &app, [&]() {
        const bool ready = QOhosJsThreadGateway::eval(
            [](QOhosJsState& state) {
                const auto ability = state.defaultQAbility();
                return ability && ability->Get("context").IsObject();
            },
            Q_FUNC_INFO);
        if (!ready || !window.isVisible()) {
            if (elapsed.elapsed() < 15000)
                return;
            readyTimer.stop();
            marker(QStringLiteral("QGC_RUNTIME_DONE phase=%1 exit=2 context-not-ready").arg(phase));
            app.exit(2);
            return;
        }
        readyTimer.stop();
        marker(QStringLiteral("QGC_RUNTIME_READY phase=%1").arg(phase.isEmpty() ? QStringLiteral("idle") : phase));
        if (phase.isEmpty())
            return;
        if (phase != QStringLiteral("core") && phase != QStringLiteral("grant") && phase != QStringLiteral("deny")) {
            marker(QStringLiteral("QGC_RUNTIME_DONE phase=%1 exit=2 invalid-phase").arg(phase));
            app.exit(2);
            return;
        }
        RuntimeTest test;
        QStringList arguments = app.arguments();
        const QStringList core{QStringLiteral("factoriesReady"),         QStringLiteral("sppParametersAndBinary"),
                               QStringLiteral("bleParametersAndBinary"), QStringLiteral("errorPropagation"),
                               QStringLiteral("lateCloseIsolation"),     QStringLiteral("productionSnapshot")};
        const QStringList permissions{QStringLiteral("permissionRequest"), QStringLiteral("permissionRepeat")};
        bool selected = false;
        for (const QString& argument : arguments.mid(1)) {
            if (core.contains(argument.section(':', 0, 0)) || permissions.contains(argument.section(':', 0, 0)))
                selected = true;
        }
        if (!selected)
            arguments.append(phase == QStringLiteral("core") ? core : permissions);
        const int result = QTest::qExec(&test, arguments);
        marker(QStringLiteral("QGC_RUNTIME_DONE phase=%1 exit=%2").arg(phase).arg(result));
        app.exit(result);
    });
    readyTimer.start();
    return app.exec();
}

#include "RuntimeTest.moc"

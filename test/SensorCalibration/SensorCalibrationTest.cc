#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtGui/QGuiApplication>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <algorithm>
#include <array>
#include <cstring>

#include "MAVLinkLib.h"
#include "QGCApplication.h"
#include "SensorsComponentController.h"
#include "StatusTextHandler.h"
#include "Vehicle.h"

mavlink_status_t m_mavlink_status[MAVLINK_COMM_NUM_BUFFERS]{};

mavlink_status_t* mavlink_get_channel_status(uint8_t channel)
{
    return &m_mavlink_status[channel];
}

class CalibrationItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(double value MEMBER value)
    Q_PROPERTY(QString text MEMBER text)
public:
    double value = 0;
    QString text;
    QStringList lines;

    Q_INVOKABLE void append(QString line) { lines.append(line); }
};

struct Side
{
    const char* wire;
    const char* property;
};

static const std::array<Side, 6> sides{{{"down", "Down"},
                                        {"up", "UpsideDown"},
                                        {"left", "Left"},
                                        {"right", "Right"},
                                        {"front", "NoseDown"},
                                        {"back", "TailDown"}}};

class CalibrationSession
{
public:
    Vehicle vehicle;
    CalibrationItem log, progress, help;
    StatusTextHandler status;
    std::unique_ptr<SensorsComponentController> controller;
    QStringList decodedTexts;
    int emittedVehicleId = 1;
    uint16_t nextId = 1;
    bool framed = false;

    CalibrationSession()
    {
        calibrationTestVehicle = &vehicle;
        qgcApp()->messages.clear();
        controller = std::make_unique<SensorsComponentController>();
        const std::array<std::pair<const char*, CalibrationItem*>, 3> items{
            {{"statusLog", &log}, {"progressBar", &progress}, {"orientationCalAreaHelpText", &help}}};
        for (const auto& item : items) {
            if (!controller->setProperty(item.first, QVariant::fromValue<QQuickItem*>(item.second))) {
                qFatal("Missing production item property: %s", item.first);
            }
        }
        QObject::connect(&status, &StatusTextHandler::textMessageReceived, &vehicle,
                         [this](MAV_COMPONENT component, MAV_SEVERITY severity, QString text, QString description) {
                             decodedTexts.append(text);
                             // Vehicle::_textMessageReceived forwards these raw fields to this signal.
                             emit vehicle.textMessageReceived(emittedVehicleId, component, severity, text, description);
                         });
    }

    void send(const QString& text, uint16_t id, uint8_t chunk = 0, int sysid = 1, int compid = MAV_COMP_ID_AUTOPILOT1,
              int severity = MAV_SEVERITY_INFO)
    {
        const QByteArray utf8 = text.toUtf8();
        if (utf8.size() > MAVLINK_MSG_STATUSTEXT_FIELD_TEXT_LEN) {
            qFatal("Each MAVLink STATUSTEXT chunk must fit its actual 50-byte wire field");
        }
        char payload[MAVLINK_MSG_STATUSTEXT_FIELD_TEXT_LEN]{};
        std::memcpy(payload, utf8.constData(), utf8.size());
        mavlink_message_t message{};
        mavlink_msg_statustext_pack_chan(sysid, compid, MAVLINK_COMM_0, &message, severity, payload, id, chunk);
        uint8_t bytes[MAVLINK_MAX_PACKET_LEN]{};
        const auto length = mavlink_msg_to_send_buffer(bytes, &message);
        framed = false;
        for (int i = 0; i < length; ++i) {
            mavlink_message_t received{};
            mavlink_status_t parser{};
            if (mavlink_parse_char(MAVLINK_COMM_1, bytes[i], &received, &parser)) {
                framed = true;
                emittedVehicleId = received.sysid;
                status.mavlinkMessageReceived(received);
            }
        }
        if (!framed) {
            qFatal("The real MAVLink frame failed CRC/decode");
        }
    }

    void send(const QString& text) { send(text, nextId++); }

    void startAccel()
    {
        controller->calibrateAccel();
        send(QStringLiteral("[cal] calibration started: 2 accel"));
    }

    int sideState(const Side& side) const
    {
        const QByteArray name = QByteArrayLiteral("orientationCal") + side.property + "SideState";
        return controller->property(name.constData()).toInt();
    }

    bool flag(const Side& side, const char* suffix) const
    {
        if (QByteArray(suffix) == "Done")
            return sideState(side) == SensorsComponentController::SideCalStateCompleted;
        if (QByteArray(suffix) == "InProgress")
            return sideState(side) == SensorsComponentController::SideCalStateInProgress;
        const QByteArray name = QByteArrayLiteral("orientationCal") + side.property + "Side" + suffix;
        return controller->property(name.constData()).toBool();
    }

    QList<int> state() const
    {
        QList<int> result;
        for (const auto& side : sides) {
            result << sideState(side) << flag(side, "Visible");
        }
        return result;
    }

    void detected(const Side& side) { send(QStringLiteral("[cal] %1 orientation detected").arg(side.wire)); }

    void done(const Side& side)
    {
        send(QStringLiteral("[cal] %1 side done, rotate to a different side").arg(side.wire));
    }
};

class SensorCalibrationTest : public QObject
{
    Q_OBJECT
private slots:

    void initTestCase()
    {
        qRegisterMetaType<MAV_COMPONENT>();
        qRegisterMetaType<MAV_SEVERITY>();
        qInfo("sourceSnapshot=%s", CALIBRATION_SOURCE_SNAPSHOT);
    }

    void sixOrientations_data()
    {
        QTest::addColumn<QList<int>>("order");
        QTest::addColumn<bool>("nonzeroIds");
        const QList<QList<int>> orders{{0, 4, 5, 2, 3, 1}, {5, 4, 3, 2, 1, 0}, {2, 1, 3, 5, 0, 4}};
        for (int i = 0; i < orders.size(); ++i) {
            for (bool ids : {false, true}) {
                QTest::newRow(qPrintable(QString("order-%1-id-%2").arg(i).arg(ids ? "nonzero" : "zero")))
                    << orders[i] << ids;
            }
        }
    }

    void sixOrientations()
    {
        QFETCH(QList<int>, order);
        QFETCH(bool, nonzeroIds);
        CalibrationSession session;
        session.controller->calibrateAccel();
        uint16_t id = 40;
        const auto send = [&](const QString& text) { session.send(text, nonzeroIds ? id++ : 0); };
        send(QStringLiteral("[cal] calibration started: 2 accel"));
        QVERIFY(session.controller->property("showOrientationCalArea").toBool());
        QVERIFY(session.controller->calibrationActive());
        for (const auto& side : sides) {
            QVERIFY(session.flag(side, "Visible"));
            QVERIFY(!session.flag(side, "Done"));
        }
        QSignalSpy states(session.controller.get(), &SensorsComponentController::orientationCalSidesStateChanged);
        QList<int> done;
        for (int i : order) {
            const auto& side = sides[i];
            send(QStringLiteral("[cal] %1 orientation detected").arg(side.wire));
            QVERIFY(session.flag(side, "InProgress"));
            QVERIFY(!session.controller->property("magCalInProgress").toBool());
            send(QStringLiteral("[cal] %1 side done, rotate to a different side").arg(side.wire));
            done.append(i);
            QVERIFY(!session.flag(side, "InProgress"));
            for (int j = 0; j < 6; ++j) {
                QCOMPARE(session.flag(sides[j], "Done"), done.contains(j));
            }
            QCOMPARE(session.vehicle.starts.size(), 1);
            QCOMPARE(session.vehicle.starts.first(), int(QGCMAVLink::CalibrationAccel));
            QCOMPARE(session.vehicle.stops, 0);
            QCOMPARE(session.vehicle.otherCommands, 0);
        }
        QCOMPARE(states.size(), 12);
        send(QStringLiteral("[cal] calibration done: accel"));
        QCOMPARE(session.progress.value, 1.0);
        QVERIFY(!session.controller->calibrationActive());
        QCOMPARE(session.help.text, QStringLiteral("Calibration complete"));
        QCOMPARE(session.vehicle.parameterManager()->refreshed.size(), 8);
        QCOMPARE(session.vehicle.parameterManager()->refreshed.mid(6), QStringList({"CAL_*", "SENS_*"}));
        QVERIFY(qgcApp()->messages.isEmpty());
    }

    void secondToThirdNeedsNoNewCommand()
    {
        CalibrationSession session;
        session.startAccel();
        session.detected(sides[0]);
        session.done(sides[0]);
        session.detected(sides[4]);
        session.done(sides[4]);
        session.send(QStringLiteral("[cal] progress <34>"));
        QCOMPARE(session.progress.value, double(float(0.34)));
        const auto starts = session.vehicle.starts;
        session.send(QStringLiteral("[cal] pending: back left right up"));
        session.send(QStringLiteral("[cal] detected motion, hold still..."));
        QVERIFY(!session.flag(sides[5], "InProgress"));
        session.detected(sides[5]);
        QVERIFY(session.flag(sides[5], "InProgress"));
        QVERIFY(session.flag(sides[0], "Done"));
        QVERIFY(session.flag(sides[4], "Done"));
        session.done(sides[5]);
        session.send(QStringLiteral("[cal] progress <51>"));
        QVERIFY(session.flag(sides[5], "Done"));
        QCOMPARE(session.progress.value, double(float(0.51)));
        QCOMPARE(session.vehicle.starts, starts);
        QCOMPARE(session.vehicle.stops, 0);
        QCOMPARE(session.vehicle.otherCommands, 0);
    }

    void statusTextWireLength_data()
    {
        QTest::addColumn<int>("length");
        for (int length : {46, 47, 48, 49, 50}) {
            QTest::newRow(qPrintable(QString::number(length))) << length;
        }
    }

    void statusTextWireLength()
    {
        QFETCH(int, length);
        CalibrationSession session;
        session.startAccel();
        const QString text = QStringLiteral("[cal] ") + QString(length - 6, QChar('x'));
        session.send(text, length == 50 ? 0 : session.nextId++);
        QCOMPARE(session.decodedTexts.last(), text);
        QCOMPARE(session.decodedTexts.last().toUtf8().size(), length);
        QCOMPARE(session.log.lines.last(), text);
    }

    void progressValues_data()
    {
        QTest::addColumn<QString>("wire");
        QTest::addColumn<int>("percent");
        for (int percent : {0, 17, 34, 51, 68, 85, 100}) {
            QTest::newRow(qPrintable(QString::number(percent)))
                << QString("[cal] progress <%1>").arg(percent) << percent;
        }
        QTest::newRow("html-escaped") << QStringLiteral("[cal] progress &lt;34&gt;") << 34;
    }

    void progressValues()
    {
        QFETCH(QString, wire);
        QFETCH(int, percent);
        CalibrationSession session;
        session.startAccel();
        session.send(wire);
        QCOMPARE(session.progress.value, double(float(percent / 100.0)));
        const double value = session.progress.value;
        session.send(QStringLiteral("[cal] progress <invalid>"));
        QCOMPARE(session.progress.value, value);
    }

    void invalidOrientationMessages_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("sysid");
        QTest::newRow("prefix-missing") << QStringLiteral("down orientation detected") << 1;
        QTest::newRow("prefix-case") << QStringLiteral("[Cal] down orientation detected") << 1;
        QTest::newRow("prefix-leading-noise") << QStringLiteral("noise [cal] down orientation detected") << 1;
        QTest::newRow("suffix-incomplete") << QStringLiteral("[cal] down orientation detecte") << 1;
        QTest::newRow("suffix-trailing-noise") << QStringLiteral("[cal] down orientation detected ignored") << 1;
        QTest::newRow("unknown-side") << QStringLiteral("[cal] diagonal orientation detected") << 1;
        QTest::newRow("other-vehicle") << QStringLiteral("[cal] down orientation detected") << 2;
        QTest::newRow("invalid-done") << QStringLiteral("[cal] down side done, rotate elsewhere") << 1;
    }

    void invalidOrientationMessages()
    {
        QFETCH(QString, text);
        QFETCH(int, sysid);
        CalibrationSession session;
        session.startAccel();
        const auto before = session.state();
        session.send(text, 50, 0, sysid);
        QCOMPARE(session.state(), before);
        QCOMPARE(session.progress.value, 0.0);
        QCOMPARE(session.vehicle.starts.size(), 1);
        QCOMPARE(session.vehicle.stops, 0);
    }

    void repeatedCompletedSideIsStable()
    {
        CalibrationSession session;
        session.startAccel();
        session.detected(sides[0]);
        session.done(sides[0]);
        const auto state = session.state();
        session.done(sides[0]);
        session.send(QStringLiteral("[cal] down side already completed"));
        QCOMPARE(session.state(), state);
        QVERIFY(session.help.text.contains("already completed"));
        session.detected(sides[4]);
        session.done(sides[4]);
        session.detected(sides[5]);
        QVERIFY(session.flag(sides[5], "InProgress"));
        QCOMPARE(session.vehicle.starts.size(), 1);
    }

    void stopOutcomes_data()
    {
        QTest::addColumn<QString>("message");
        QTest::addColumn<bool>("requestedCancel");
        QTest::addColumn<bool>("success");
        QTest::newRow("done") << QStringLiteral("[cal] calibration done: accel") << false << true;
        QTest::newRow("requested-cancel") << QStringLiteral("[cal] calibration cancelled") << true << false;
        QTest::newRow("unexpected-cancel") << QStringLiteral("[cal] calibration cancelled") << false << false;
        QTest::newRow("failed") << QStringLiteral("[cal] calibration failed: accel") << false << false;
    }

    void stopOutcomes()
    {
        QFETCH(QString, message);
        QFETCH(bool, requestedCancel);
        QFETCH(bool, success);
        CalibrationSession session;
        session.startAccel();
        session.detected(sides[0]);
        session.done(sides[0]);
        QSignalSpy reset(session.controller.get(), &SensorsComponentController::resetStatusTextArea);
        if (requestedCancel) {
            session.controller->cancelCalibration();
            QVERIFY(session.controller->property("waitingForCancel").toBool());
        }
        session.send(message);
        QCOMPARE(session.progress.value, success ? 1.0 : 0.0);
        QVERIFY(!session.controller->property("waitingForCancel").toBool());
        QVERIFY(!session.controller->calibrationActive());
        QCOMPARE(session.vehicle.stops, requestedCancel ? 1 : 0);
        QCOMPARE(session.vehicle.starts.size(), 1);
        QCOMPARE(qgcApp()->messages.size(), success || requestedCancel ? 0 : 1);
        QCOMPARE(reset.size(), success || requestedCancel ? 1 : 0);
        QCOMPARE(session.vehicle.parameterManager()->refreshed.size(), 8);
        const auto before = session.state();
        session.send(QStringLiteral("[cal] left orientation detected"));
        QCOMPARE(session.state(), before);  // Terminal states disconnect the production slot.
    }

    void chunkedTextReassemblesBeforeUiUpdate()
    {
        CalibrationSession session;
        session.startAccel();
        const QString text = QStringLiteral("[cal] front ") + QString(24, QChar(' ')) +
                             QStringLiteral("side done, rotate to a different side");
        const int before = session.decodedTexts.size();
        session.send(text.left(50), 400, 0);
        QCOMPARE(session.decodedTexts.size(), before);
        QVERIFY(!session.flag(sides[4], "Done"));
        session.send(text.mid(50), 400, 1);
        QCOMPARE(session.decodedTexts.size(), before + 1);
        QCOMPARE(session.decodedTexts.last(), text);
        QVERIFY(session.flag(sides[4], "Done"));
    }

    void missingChunkIsMarked()
    {
        CalibrationSession session;
        session.startAccel();
        const QString first = QStringLiteral("[cal] ") + QString(44, QChar('x'));
        session.send(first, 401, 0);
        session.send(QStringLiteral("end"), 401, 2);
        QCOMPARE(session.decodedTexts.last(), first + QStringLiteral(" ... end"));
        QVERIFY(!session.flag(sides[4], "Done"));
    }

    void chunkTimeoutEmitsIncompleteText()
    {
        CalibrationSession session;
        session.startAccel();
        const QString first = QStringLiteral("[cal] ") + QString(44, QChar('x'));
        const int before = session.decodedTexts.size();
        session.send(first, 402, 0);
        QCOMPARE(session.decodedTexts.size(), before);
        QTRY_COMPARE_WITH_TIMEOUT(session.decodedTexts.size(), before + 1, 2000);
        QCOMPARE(session.decodedTexts.last(), first + QStringLiteral(" ... "));
        QVERIFY(!session.flag(sides[0], "Done"));
        session.detected(sides[0]);
        QVERIFY(session.flag(sides[0], "InProgress"));
    }

    void realPadAccelReplay_data()
    {
        QTest::addColumn<QString>("name");
        QTest::newRow("three-sides-then-no-motion-timeout") << QStringLiteral("first");
        QTest::newRow("two-sides-then-motion-and-cancel") << QStringLiteral("second");
    }

    void realPadAccelReplay()
    {
        QFETCH(QString, name);
        QFile fixture(QStringLiteral(CALIBRATION_REPLAY_PATH));
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(fixture.readAll(), &error);
        QCOMPARE(error.error, QJsonParseError::NoError);
        const QJsonArray messages = document.object().value("sessions").toObject().value(name).toArray();
        QVERIFY(!messages.isEmpty());
        CalibrationSession session;
        session.controller->calibrateAccel();
        bool sawSecondDone = false;
        bool sawThirdDone = false;
        qint64 previousMs = -1;
        for (const auto& value : messages) {
            const auto row = value.toObject();
            const qint64 relativeMs = row.value("relativeMs").toInteger();
            QVERIFY(relativeMs >= previousMs);
            previousMs = relativeMs;
            QVERIFY(row.value("id").toInt() != 0);
            const QString text = row.value("text").toString();
            const int emitted = session.decodedTexts.size();
            session.send(text, row.value("id").toInt(), row.value("chunk").toInt(), row.value("sysid").toInt(),
                         row.value("compid").toInt(), row.value("severity").toInt(6));
            QCOMPARE(session.decodedTexts.size(), emitted + 1);
            QCOMPARE(session.decodedTexts.last(), text);
            if (text == "[cal] progress <34>") {
                QCOMPARE(session.progress.value, double(float(0.34)));
            }
            if (text == "[cal] front side done, rotate to a different side") {
                QVERIFY(session.flag(sides[0], "Done"));
                QVERIFY(session.flag(sides[4], "Done"));
                QVERIFY(!session.flag(sides[5], "Done"));
                sawSecondDone = true;
            }
            if (text == "[cal] back side done, rotate to a different side") {
                QVERIFY(sawSecondDone);
                QVERIFY(session.flag(sides[5], "Done"));
                QCOMPARE(session.progress.value, double(float(0.51)));
                sawThirdDone = true;
            }
            if (sawSecondDone &&
                (text.startsWith("[cal] detected motion") || text.startsWith("[cal] detected rest position"))) {
                QVERIFY(session.controller->property("showOrientationCalArea").toBool());
                QVERIFY(session.controller->calibrationActive());
                QVERIFY(!session.controller->property("waitingForCancel").toBool());
                QCOMPARE(session.progress.value, double(float(sawThirdDone ? 0.51 : 0.34)));
                QVERIFY(qgcApp()->messages.isEmpty());
            }
            QCOMPARE(session.vehicle.starts.size(), 1);
            QCOMPARE(session.vehicle.stops, 0);
            QCOMPARE(session.vehicle.otherCommands, 0);
        }
        QVERIFY(sawSecondDone);
        QCOMPARE(sawThirdDone, name == "first");
        QCOMPARE(qgcApp()->messages.size(), 1);
        QVERIFY(!session.controller->property("showOrientationCalArea").toBool());
        QCOMPARE(session.progress.value, 0.0);
    }
};

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    QGuiApplication application(argc, argv);
    SensorCalibrationTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "SensorCalibrationTest.moc"

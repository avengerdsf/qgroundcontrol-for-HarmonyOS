#include "AudioOutput.h"
#include "Fact.h"
#include "FakeTextToSpeech.h"
#include "QGCApplication.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QThread>
#include <QtCore/QTranslator>
#include <QtQuick/QQuickWindow>
#include <QtTest/QTest>

class AudioTranslator : public QTranslator
{
public:
    bool isEmpty() const override { return false; }
    QString translate(const char *context, const char *source, const char *, int) const override
    {
        return QByteArray(context) == "AudioOutput" && QByteArray(source) == "%1"
            ? QStringLiteral("translated: %1") : QString();
    }
};

class AudioOutputTest : public QObject
{
    Q_OBJECT

private:
    QGCApplication _application;

    static void finishFirstFrame(QQuickWindow &window)
    {
        QVERIFY(QMetaObject::invokeMethod(&window, "frameSwapped", Qt::DirectConnection));
        QCoreApplication::processEvents();
    }

private slots:
    void init()
    {
        _application.rootWindow = nullptr;
        _application.unitTesting = false;
        audioStartupTestApplication = &_application;
        QTextToSpeech::enginesAvailable = true;
        QTextToSpeech::engineSucceeds = true;
        QTextToSpeech::constructed = 0;
        QTextToSpeech::engineSelections = 0;
        QTextToSpeech::lastCreated = nullptr;
    }

    void waitsForFirstFrameAndInitializesOnce()
    {
        QQuickWindow window;
        _application.rootWindow = &window;
        Fact muted(false);
        AudioOutput audio;
        audio.init(&muted);
        audio.init(&muted);
        QCOMPARE(QTextToSpeech::constructed, 0);
        QVERIFY(QMetaObject::invokeMethod(&window, "frameSwapped", Qt::DirectConnection));
        QCOMPARE(QTextToSpeech::constructed, 0);
        QCoreApplication::processEvents();
        QCOMPARE(QTextToSpeech::engineSelections, 1);
        finishFirstFrame(window);
        audio.init(&muted);
        QCOMPARE(QTextToSpeech::engineSelections, 1);
    }

    void keepsPendingAnnouncementsInOrderWithTextMods()
    {
        QQuickWindow window;
        _application.rootWindow = &window;
        Fact muted(false);
        AudioOutput audio;
        AudioTranslator translator;
        QCoreApplication::installTranslator(&translator);
        audio.init(&muted);
        audio.say(QStringLiteral("WP -10m"));
        audio.say(QStringLiteral("RTL"), AudioOutput::TextMod::Translate);
        audio.say(QStringLiteral("1000ms"));
        finishFirstFrame(window);
        QCOMPARE(QTextToSpeech::lastCreated->spoken,
            (QStringList{"waypoint negative 10 meters", "translated: return To launch", "1 second"}));
        QCoreApplication::removeTranslator(&translator);
    }

    void tracksMuteWhileStartupIsPending()
    {
        QQuickWindow window;
        _application.rootWindow = &window;
        Fact muted(true);
        AudioOutput audio;
        audio.init(&muted);
        QVERIFY(audio.isMuted());
        audio.say(QStringLiteral("discarded while muted"));
        muted.setRawValue(false);
        QVERIFY(!audio.isMuted());
        audio.say(QStringLiteral("retained"));
        muted.setRawValue(true);
        muted.setRawValue(false);
        finishFirstFrame(window);
        QCOMPARE(QTextToSpeech::lastCreated->spoken, QStringList{"retained"});
        muted.setRawValue(true);
        QCOMPARE(QTextToSpeech::lastCreated->volume, 0.0);
        audio.say(QStringLiteral("still muted"));
        QCOMPARE(QTextToSpeech::lastCreated->spoken, QStringList{"retained"});
    }

    void appliesLatestMuteBeforeReplayingPendingSpeech()
    {
        QQuickWindow window;
        _application.rootWindow = &window;
        Fact muted(false);
        AudioOutput audio;
        audio.init(&muted);
        audio.say(QStringLiteral("pending"));
        muted.setRawValue(true);
        finishFirstFrame(window);
        QCOMPARE(QTextToSpeech::lastCreated->volume, 0.0);
        QVERIFY(QTextToSpeech::lastCreated->spoken.isEmpty());
    }

    void boundsPendingSpeechWithExistingOverflowPolicy()
    {
        QQuickWindow window;
        _application.rootWindow = &window;
        Fact muted(false);
        AudioOutput audio;
        audio.init(&muted);
        for (int i = 0; i < 21; ++i) {
            audio.say(QStringLiteral("message %1").arg(i));
        }
        finishFirstFrame(window);
        QCOMPARE(QTextToSpeech::lastCreated->spoken, QStringList{"message 20"});
    }

    void dropsFailedStartupSpeechAndAllowsRetry()
    {
        QQuickWindow window;
        _application.rootWindow = &window;
        Fact muted(false);
        AudioOutput audio;
        QTextToSpeech::engineSucceeds = false;
        audio.init(&muted);
        audio.say(QStringLiteral("failed attempt"));
        finishFirstFrame(window);
        QVERIFY(QTextToSpeech::lastCreated->spoken.isEmpty());
        audio.say(QStringLiteral("not initialized"));
        QTextToSpeech::engineSucceeds = true;
        audio.init(&muted);
        audio.say(QStringLiteral("retry"));
        finishFirstFrame(window);
        QCOMPARE(QTextToSpeech::engineSelections, 2);
        QCOMPARE(QTextToSpeech::lastCreated->spoken, QStringList{"retry"});
    }

    void recoversWhenNoEnginesWereAvailable()
    {
        QQuickWindow window;
        _application.rootWindow = &window;
        Fact muted(false);
        AudioOutput audio;
        QTextToSpeech::enginesAvailable = false;
        audio.init(&muted);
        audio.say(QStringLiteral("unavailable"));
        finishFirstFrame(window);
        QTextToSpeech::enginesAvailable = true;
        audio.init(&muted);
        audio.say(QStringLiteral("available"));
        finishFirstFrame(window);
        QCOMPARE(QTextToSpeech::lastCreated->spoken, QStringList{"available"});
    }

    void cancelsStartupWhenAudioOutputIsDestroyed()
    {
        QQuickWindow window;
        _application.rootWindow = &window;
        Fact muted(false);
        auto *audio = new AudioOutput;
        audio->init(&muted);
        audio->say(QStringLiteral("destroyed"));
        QVERIFY(QMetaObject::invokeMethod(&window, "frameSwapped", Qt::DirectConnection));
        delete audio;
        finishFirstFrame(window);
        QCOMPARE(QTextToSpeech::constructed, 0);
    }

    void cancelsStartupWhenMuteFactIsDestroyed()
    {
        QQuickWindow window;
        _application.rootWindow = &window;
        auto *muted = new Fact(false);
        AudioOutput audio;
        audio.init(muted);
        QVERIFY(QMetaObject::invokeMethod(&window, "frameSwapped", Qt::DirectConnection));
        delete muted;
        finishFirstFrame(window);
        QCOMPARE(QTextToSpeech::constructed, 0);
    }

    void initializesImmediatelyWithoutAWindow()
    {
        Fact muted(false);
        AudioOutput audio;
        audio.init(&muted);
        QCOMPARE(QTextToSpeech::engineSelections, 1);
        audio.say(QStringLiteral("headless"));
        QCOMPARE(QTextToSpeech::lastCreated->spoken, QStringList{"headless"});
    }

    void initializesImmediatelyForUnitTests()
    {
        QQuickWindow window;
        _application.rootWindow = &window;
        _application.unitTesting = true;
        Fact muted(false);
        AudioOutput audio;
        audio.init(&muted);
        QCOMPARE(QTextToSpeech::engineSelections, 1);
    }

    void acceptsWorkerThreadAnnouncementsDuringStartup()
    {
        QQuickWindow window;
        _application.rootWindow = &window;
        Fact muted(false);
        AudioOutput audio;
        audio.init(&muted);
        QThread *worker = QThread::create([&audio] { audio.say(QStringLiteral("worker")); });
        worker->start();
        QVERIFY(worker->wait());
        delete worker;
        QCoreApplication::processEvents();
        finishFirstFrame(window);
        QCOMPARE(QTextToSpeech::lastCreated->spoken, QStringList{"worker"});
    }
};

QTEST_MAIN(AudioOutputTest)
#include "AudioStartupTest.moc"

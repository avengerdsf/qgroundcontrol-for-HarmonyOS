#include "AudioOutput.h"
#include "Fact.h"
#include "FakeTextToSpeech.h"
#include "QGCApplication.h"

#include <QtQuick/QQuickWindow>
#include <QtTest/QTest>

class AudioOutputTest : public QObject
{
    Q_OBJECT

private slots:
    void preservesSynchronousInitializationAndSpeech()
    {
        QGCApplication application;
        audioStartupTestApplication = &application;
        QQuickWindow window;
        application.rootWindow = &window;
        Fact muted(false);
        AudioOutput audio;
        QCOMPARE(QTextToSpeech::constructed, 1);
        audio.init(&muted);
        audio.init(&muted);
        QCOMPARE(QTextToSpeech::engineSelections, 1);
        audio.say(QStringLiteral("WP -10m"));
        QCOMPARE(QTextToSpeech::lastCreated->spoken, QStringList{"waypoint negative 10 meters"});
        muted.setRawValue(true);
        QCOMPARE(QTextToSpeech::lastCreated->volume, 0.0);
        audio.say(QStringLiteral("muted"));
        QCOMPARE(QTextToSpeech::lastCreated->spoken.size(), 1);
        muted.setRawValue(false);
        for (int i = 0; i < 20; ++i) {
            audio.say(QStringLiteral("message %1").arg(i));
        }
        QCOMPARE(QTextToSpeech::lastCreated->spoken, QStringList{"message 19"});
    }
};

QTEST_MAIN(AudioOutputTest)
#include "AudioSynchronousTest.moc"

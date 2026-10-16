#pragma once

#include <QtCore/QLocale>
#include <QtCore/QObject>
#include <QtCore/QStringList>

class QTextToSpeech : public QObject
{
    Q_OBJECT

public:
    enum class Capability { Speak = 1 };
    Q_DECLARE_FLAGS(Capabilities, Capability)
    enum class BoundaryHint { Default };
    Q_ENUM(BoundaryHint)
    enum class State { Ready };
    Q_ENUM(State)
    enum class ErrorReason { NoError };
    Q_ENUM(ErrorReason)

    inline static bool enginesAvailable = true;
    inline static bool engineSucceeds = true;
    inline static int constructed = 0;
    inline static int engineSelections = 0;
    inline static QTextToSpeech *lastCreated = nullptr;

    explicit QTextToSpeech(const QString &, QObject *parent = nullptr) : QObject(parent)
    {
        ++constructed;
        lastCreated = this;
    }

    static QStringList availableEngines()
    {
        return enginesAvailable ? QStringList{QStringLiteral("test")} : QStringList{};
    }
    bool setEngine(const QString &)
    {
        ++engineSelections;
        return engineSucceeds;
    }
    QList<QLocale> availableLocales() const { return {QLocale("en_US")}; }
    void setLocale(const QLocale &) {}
    Capabilities engineCapabilities() const { return Capability::Speak; }

    Q_INVOKABLE qsizetype enqueue(const QString &text)
    {
        spoken.append(text);
        return spoken.size() - 1;
    }
    Q_INVOKABLE void stop(BoundaryHint)
    {
        spoken.clear();
    }
    Q_INVOKABLE void setVolume(double value)
    {
        volume = value;
    }

    QStringList spoken;
    double volume = 1.0;

signals:
    void engineChanged(const QString &engine);
    void aboutToSynthesize(qsizetype id);
    void stateChanged(State state);
    void errorOccurred(ErrorReason reason, const QString &errorString);
    void localeChanged(const QLocale &locale);
    void volumeChanged(double volume);
    void sayingWord(const QString &word, qsizetype id, qsizetype start, qsizetype length);
};
Q_DECLARE_OPERATORS_FOR_FLAGS(QTextToSpeech::Capabilities)

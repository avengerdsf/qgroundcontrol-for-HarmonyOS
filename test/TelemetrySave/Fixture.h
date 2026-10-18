#pragma once
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QSaveFile>
#include <QtCore/QTemporaryFile>
#include <algorithm>

struct IoFaults
{
    static inline bool copyOverflow = true;
    static inline bool shortWrites = true;
    static inline bool failSourceOpen = false;
    static inline qint64 readBytes = 0;
    static inline qint64 writeBytes = 0;
    static inline qint64 maxReadRequest = 0;
    static inline qint64 maxWriteRequest = 0;
    static inline qint64 failReadAfter = -1;
    static inline qint64 failWriteAfter = -1;
    static inline int reads = 0;
    static inline int writes = 0;
    static inline int copies = 0;
    static inline bool zeroWrite = false;
    static inline bool failFlush = false;
    static inline bool collideAtPublish = false;
    static inline QString collisionPath;

    static void reset()
    {
        copyOverflow = true;
        shortWrites = true;
        failSourceOpen = false;
        readBytes = writeBytes = maxReadRequest = maxWriteRequest = 0;
        failReadAfter = failWriteAfter = -1;
        reads = writes = copies = 0;
        zeroWrite = failFlush = collideAtPublish = false;
        collisionPath.clear();
    }
};

class ControlledFile : public QFile
{
public:
    using QFile::QFile;

    bool open(QIODevice::OpenMode mode) override
    {
        if (IoFaults::failSourceOpen && mode == QIODevice::ReadOnly) {
            setErrorString(QStringLiteral("injected source open failure"));
            return false;
        }
        return QFile::open(mode);
    }

    bool copy(const QString& target)
    {
        ++IoFaults::copies;
        if (IoFaults::copyOverflow) {
            setErrorString(QStringLiteral("EOVERFLOW: Value too large for defined data type"));
            return false;
        }
        return QFile::copy(target);
    }

protected:
    qint64 readData(char* data, qint64 length) override
    {
        ++IoFaults::reads;
        IoFaults::maxReadRequest = std::max(IoFaults::maxReadRequest, length);
        if (IoFaults::failReadAfter >= 0 && IoFaults::readBytes >= IoFaults::failReadAfter) {
            setErrorString(QStringLiteral("injected read failure"));
            return -1;
        }
        const qint64 size = QFile::readData(data, length);
        if (size > 0) {
            IoFaults::readBytes += size;
        }
        return size;
    }
};

class ControlledTemporaryFile : public QTemporaryFile
{
public:
    using QTemporaryFile::QTemporaryFile;

    bool flush()
    {
        if (IoFaults::failFlush) {
            setErrorString(QStringLiteral("injected flush failure"));
            return false;
        }
        return QTemporaryFile::flush();
    }

    bool rename(const QString& target)
    {
        if (IoFaults::collideAtPublish) {
            QFile existing(target);
            if (!existing.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
                existing.write("existing-race-file") != 18) {
                qFatal("Cannot create publication collision fixture");
            }
            existing.close();
            IoFaults::collisionPath = target;
        }
        return QTemporaryFile::rename(target);
    }

protected:
    qint64 writeData(const char* data, qint64 length) override
    {
        ++IoFaults::writes;
        IoFaults::maxWriteRequest = std::max(IoFaults::maxWriteRequest, length);
        if (IoFaults::zeroWrite) {
            return 0;
        }
        if (IoFaults::failWriteAfter >= 0 && IoFaults::writeBytes >= IoFaults::failWriteAfter) {
            setErrorString(QStringLiteral("injected write failure"));
            return -1;
        }
        const qint64 size =
            QTemporaryFile::writeData(data, (IoFaults::shortWrites ? std::min<qint64>(length, 4093) : length));
        if (size > 0) {
            IoFaults::writeBytes += size;
        }
        return size;
    }
};

class ControlledSaveFile : public QSaveFile
{
public:
    explicit ControlledSaveFile(const QString& target) : QSaveFile(target), _target(target) {}

    bool commit()
    {
        if (IoFaults::collideAtPublish) {
            QFile existing(_target);
            if (!existing.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
                existing.write("existing-race-file") != 18) {
                qFatal("Cannot create publication collision fixture");
            }
            existing.close();
            IoFaults::collisionPath = _target;
        }
        if (IoFaults::failFlush) {
            setErrorString(QStringLiteral("injected flush failure"));
            cancelWriting();
            return false;
        }
        return QSaveFile::commit();
    }

protected:
    qint64 writeData(const char* data, qint64 length) override
    {
        ++IoFaults::writes;
        IoFaults::maxWriteRequest = std::max(IoFaults::maxWriteRequest, length);
        if (IoFaults::zeroWrite) {
            return 0;
        }
        if (IoFaults::failWriteAfter >= 0 && IoFaults::writeBytes >= IoFaults::failWriteAfter) {
            setErrorString(QStringLiteral("injected write failure"));
            return -1;
        }
        const qint64 size =
            QSaveFile::writeData(data, (IoFaults::shortWrites ? std::min<qint64>(length, 4093) : length));
        if (size > 0) {
            IoFaults::writeBytes += size;
        }
        return size;
    }

private:
    QString _target;
};

class ControlledDateTime
{
public:
    static QDateTime currentDateTime() { return QDateTime(QDate(2026, 10, 15), QTime(12, 34, 56)); }
};

class AppSettings
{
public:
    QString directory;

    QString telemetrySavePath() const { return directory; }

    static constexpr const char* telemetryFileExtension = "tlog";
};

class SettingsManager
{
public:
    static SettingsManager* instance()
    {
        static SettingsManager manager;
        return &manager;
    }

    AppSettings* appSettings() { return &settings; }

    AppSettings settings;
};

class TestApplication
{
public:
    QStringList messages;

    void showAppMessage(const QString& message) { messages.append(message); }
};

inline TestApplication* qgcApp()
{
    static TestApplication app;
    return &app;
}

namespace QGC {
inline void showAppMessage(const QString& message, const QString& = QString())
{
    qgcApp()->showAppMessage(message);
}
}  // namespace QGC

class MAVLinkProtocol : public QObject
{
public:
    void _saveTelemetryLog(const QString& tempLogfile);
    bool _checkTelemetrySavePath();
};

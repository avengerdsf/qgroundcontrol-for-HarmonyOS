#include "HarmonySerialIODevice.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QMutexLocker>
#include <QtCore/QThread>
#include <QtCore/QWaitCondition>
#include <cstring>
#include <utility>

struct HarmonySerialIODevice::State
{
    QMutex mutex;
    QWaitCondition changed;
    QThread thread;
    HarmonySerialBridge *bridge = nullptr;
    const std::atomic_bool *requestStop = nullptr;
    QString portName;
    HarmonySerialSettings settings;
    QByteArray received;
    qint64 pendingBytes = 0;
    quint64 generation = 0;
    quint64 ioGeneration = 0;
    bool opening = false;
    bool open = false;
    bool baudPending = false;
    bool cancelled = false;
    QSerialPort::SerialPortError error = QSerialPort::NoError;
    QString errorString;
};

HarmonySerialIODevice::HarmonySerialIODevice(const std::atomic_bool *requestStop, bool autoConnect)
    : _state(std::make_shared<State>())
{
    _state->requestStop = requestStop;
    _state->settings.autoConnect = autoConnect;
    _state->bridge = new HarmonySerialBridge();
    _state->bridge->moveToThread(&_state->thread);
    const std::weak_ptr<State> weak = _state;
    QObject::connect(&_state->thread, &QThread::finished, _state->bridge, &QObject::deleteLater);
    QObject::connect(_state->bridge, &HarmonySerialBridge::connected, _state->bridge, [weak]() {
        if (const auto state = weak.lock()) {
            const QMutexLocker locker(&state->mutex);
            if (state->generation != state->ioGeneration) { return; }
            state->opening = false;
            if (!state->cancelled) { state->open = true; }
            state->changed.wakeAll();
        }
    });
    QObject::connect(_state->bridge, &HarmonySerialBridge::disconnected, _state->bridge, [weak]() {
        if (const auto state = weak.lock()) {
            const QMutexLocker locker(&state->mutex);
            if (state->generation != state->ioGeneration) { return; }
            state->opening = false;
            state->open = false;
            state->baudPending = false;
            state->changed.wakeAll();
        }
    });
    QObject::connect(_state->bridge, &HarmonySerialBridge::dataReceived, _state->bridge, [weak](const QByteArray &bytes) {
        if (const auto state = weak.lock()) {
            const QMutexLocker locker(&state->mutex);
            if (state->generation != state->ioGeneration) { return; }
            if (state->open && !state->cancelled) { state->received.append(bytes); }
            state->changed.wakeAll();
        }
    });
    QObject::connect(_state->bridge, &HarmonySerialBridge::dataSent, _state->bridge, [weak](const QByteArray &bytes) {
        if (const auto state = weak.lock()) {
            const QMutexLocker locker(&state->mutex);
            if (state->generation != state->ioGeneration) { return; }
            state->pendingBytes = qMax<qint64>(0, state->pendingBytes - bytes.size());
            state->changed.wakeAll();
        }
    });
    QObject::connect(_state->bridge, &HarmonySerialBridge::baudRateChanged, _state->bridge, [weak](qint32 baud) {
        if (const auto state = weak.lock()) {
            const QMutexLocker locker(&state->mutex);
            if (state->generation != state->ioGeneration) { return; }
            state->settings.baud = baud;
            state->baudPending = false;
            state->changed.wakeAll();
        }
    });
    QObject::connect(_state->bridge, &HarmonySerialBridge::errorOccurred, _state->bridge, [weak](const QString &error) {
        if (const auto state = weak.lock()) {
            const QMutexLocker locker(&state->mutex);
            if (state->generation != state->ioGeneration) { return; }
            state->error = QSerialPort::ResourceError;
            state->errorString = error;
            state->opening = false;
            state->open = false;
            state->baudPending = false;
            state->changed.wakeAll();
        }
    });
    _state->thread.setObjectName(QStringLiteral("HarmonySerialIO"));
    _state->thread.start();
}

HarmonySerialIODevice::~HarmonySerialIODevice()
{
    close();
    _state->thread.quit();
    _state->thread.wait();
}

void HarmonySerialIODevice::setPortName(const QString &name)
{
    const QMutexLocker locker(&_state->mutex);
    if (!_state->open && !_state->opening) { _state->portName = name; }
}

bool HarmonySerialIODevice::setBaudRate(qint32 baud)
{
    {
        const QMutexLocker locker(&_state->mutex);
        if (!HarmonySerialBridge::supportedBaudRates().contains(baud)) { return false; }
        if (!_state->open) { _state->settings.baud = baud; return true; }
        if (_state->settings.baud == baud) { return true; }
    }
    if (!waitForBytesWritten(2000)) { return false; }
    {
        const QMutexLocker locker(&_state->mutex);
        _state->baudPending = true;
    }
    QMetaObject::invokeMethod(_state->bridge, [state = _state, baud]() {
        {
            const QMutexLocker locker(&state->mutex);
            if (!state->open || state->cancelled || state->generation != state->ioGeneration ||
                    (state->requestStop && state->requestStop->load())) { return; }
        }
        state->bridge->setBaudRate(baud);
    }, Qt::QueuedConnection);
    const bool complete = _wait([this]() { return _state->open && !_state->baudPending; }, 2000);
    if (!complete && error() == QSerialPort::TimeoutError) { close(); }
    return complete;
}

bool HarmonySerialIODevice::setDataBits(QSerialPort::DataBits bits)
{
    const QMutexLocker locker(&_state->mutex);
    if (_state->open && bits != _state->settings.dataBits) { return false; }
    _state->settings.dataBits = bits;
    return true;
}

bool HarmonySerialIODevice::setParity(QSerialPort::Parity parity)
{
    const QMutexLocker locker(&_state->mutex);
    if (_state->open && parity != _state->settings.parity) { return false; }
    _state->settings.parity = parity;
    return true;
}

bool HarmonySerialIODevice::setStopBits(QSerialPort::StopBits bits)
{
    const QMutexLocker locker(&_state->mutex);
    if (_state->open && bits != _state->settings.stopBits) { return false; }
    _state->settings.stopBits = bits;
    return true;
}

bool HarmonySerialIODevice::setFlowControl(QSerialPort::FlowControl flow)
{
    const QMutexLocker locker(&_state->mutex);
    if (_state->open && flow != _state->settings.flowControl) { return false; }
    _state->settings.flowControl = flow;
    return true;
}

bool HarmonySerialIODevice::open(QIODevice::OpenMode mode)
{
    QString portName;
    HarmonySerialSettings settings;
    quint64 generation;
    {
        const QMutexLocker locker(&_state->mutex);
        if (mode != QIODevice::ReadWrite || (QCoreApplication::instance() && QThread::currentThread() == QCoreApplication::instance()->thread())) {
            _state->error = QSerialPort::UnsupportedOperationError;
            _state->errorString = QStringLiteral("Blocking USB serial protocols require a worker thread and ReadWrite mode");
            return false;
        }
        if (_state->open || _state->opening) { return false; }
        _state->cancelled = false;
        _state->error = QSerialPort::NoError;
        _state->errorString.clear();
        _state->received.clear();
        _state->pendingBytes = 0;
        _state->opening = true;
        generation = ++_state->generation;
        portName = _state->portName;
        settings = _state->settings;
    }
    QMetaObject::invokeMethod(_state->bridge, [state = _state, portName, settings, generation]() {
        {
            const QMutexLocker locker(&state->mutex);
            if (state->generation != generation || state->cancelled) { return; }
            state->ioGeneration = generation;
        }
        state->bridge->open(portName, settings);
    }, Qt::QueuedConnection);
    if (!_wait([this]() { return _state->open && !_state->opening; }, 30000)) { close(); return false; }
    return true;
}

void HarmonySerialIODevice::close()
{
    {
        const QMutexLocker locker(&_state->mutex);
        ++_state->generation;
        _state->opening = false;
        _state->open = false;
        _state->baudPending = false;
        _state->received.clear();
        _state->pendingBytes = 0;
        _state->changed.wakeAll();
    }
    QMetaObject::invokeMethod(_state->bridge, &HarmonySerialBridge::close, Qt::QueuedConnection);
}

void HarmonySerialIODevice::cancel()
{
    {
        const QMutexLocker locker(&_state->mutex);
        if (_state->cancelled) { return; }
        _state->cancelled = true;
        _state->error = QSerialPort::ResourceError;
        _state->errorString = QStringLiteral("USB serial operation canceled");
    }
    close();
}

bool HarmonySerialIODevice::_wait(const std::function<bool()> &predicate, int timeout)
{
    QElapsedTimer timer;
    timer.start();
    const int boundedTimeout = timeout < 0 ? 2000 : timeout;
    bool cancelPort = false;
    {
        const QMutexLocker locker(&_state->mutex);
        for (;;) {
            if (_state->cancelled || (_state->requestStop && _state->requestStop->load())) {
                _state->cancelled = true;
                _state->open = false;
                _state->opening = false;
                _state->error = QSerialPort::ResourceError;
                _state->errorString = QStringLiteral("USB serial operation canceled");
                cancelPort = true;
                break;
            }
            if (_state->error != QSerialPort::NoError && _state->error != QSerialPort::TimeoutError) { break; }
            if (predicate()) { return true; }
            if (!_state->open && !_state->opening) {
                _state->error = QSerialPort::NotOpenError;
                _state->errorString = QStringLiteral("USB serial port is closed");
                break;
            }
            const qint64 remaining = boundedTimeout - timer.elapsed();
            if (remaining <= 0) {
                _state->error = QSerialPort::TimeoutError;
                _state->errorString = QStringLiteral("USB serial operation timed out");
                break;
            }
            _state->changed.wait(&_state->mutex, qMin<qint64>(remaining, 25));
        }
    }
    if (cancelPort) { QMetaObject::invokeMethod(_state->bridge, &HarmonySerialBridge::close, Qt::QueuedConnection); }
    return false;
}

bool HarmonySerialIODevice::isOpen() const
{
    const QMutexLocker locker(&_state->mutex);
    return _state->open && !_state->cancelled && !(_state->requestStop && _state->requestStop->load());
}

qint64 HarmonySerialIODevice::bytesAvailable() const
{
    const QMutexLocker locker(&_state->mutex);
    return _state->received.size();
}

qint64 HarmonySerialIODevice::read(char *data, qint64 size)
{
    const QMutexLocker locker(&_state->mutex);
    if (!_state->open || _state->cancelled || (_state->requestStop && _state->requestStop->load()) || size < 0) { return -1; }
    const qint64 count = qMin<qint64>(size, _state->received.size());
    if (count > 0) { std::memcpy(data, _state->received.constData(), count); _state->received.remove(0, count); }
    return count;
}

QByteArray HarmonySerialIODevice::readAll()
{
    const QMutexLocker locker(&_state->mutex);
    return std::exchange(_state->received, {});
}

qint64 HarmonySerialIODevice::write(const char *data, qint64 size)
{
    {
        const QMutexLocker locker(&_state->mutex);
        if (!_state->open || _state->cancelled || (_state->requestStop && _state->requestStop->load()) || size < 0) { return -1; }
        if (!size) { return 0; }
        _state->pendingBytes += size;
    }
    const QByteArray bytes(data, size);
    QMetaObject::invokeMethod(_state->bridge, [state = _state, bytes]() {
        {
            const QMutexLocker locker(&state->mutex);
            if (!state->open || state->cancelled || state->generation != state->ioGeneration ||
                    (state->requestStop && state->requestStop->load())) { return; }
        }
        state->bridge->write(bytes);
    }, Qt::QueuedConnection);
    return size;
}

bool HarmonySerialIODevice::waitForReadyRead(int timeout)
{
    return _wait([this]() { return _state->open && !_state->received.isEmpty(); }, timeout);
}

bool HarmonySerialIODevice::waitForBytesWritten(int timeout)
{
    const bool complete = _wait([this]() { return _state->open && !_state->pendingBytes; }, timeout);
    if (!complete && error() == QSerialPort::TimeoutError) { close(); }
    return complete;
}

bool HarmonySerialIODevice::flush() { return waitForBytesWritten(2000); }

QSerialPort::SerialPortError HarmonySerialIODevice::error() const
{
    const QMutexLocker locker(&_state->mutex);
    if (_state->requestStop && _state->requestStop->load()) { return QSerialPort::ResourceError; }
    return _state->error;
}

QString HarmonySerialIODevice::errorString() const
{
    const QMutexLocker locker(&_state->mutex);
    if (_state->requestStop && _state->requestStop->load()) { return QStringLiteral("USB serial operation canceled"); }
    return _state->errorString;
}

void HarmonySerialIODevice::clearError()
{
    const QMutexLocker locker(&_state->mutex);
    _state->error = QSerialPort::NoError;
    _state->errorString.clear();
}

#pragma once

#include "HarmonySerialBridge.h"
#include <QtCore/QIODevice>
#include <atomic>
#include <memory>

// Blocking protocol workers use this adapter; the bridge runs on its own event loop.
class HarmonySerialIODevice
{
public:
    explicit HarmonySerialIODevice(const std::atomic_bool *requestStop = nullptr, bool autoConnect = false);
    ~HarmonySerialIODevice();
    HarmonySerialIODevice(const HarmonySerialIODevice &) = delete;
    HarmonySerialIODevice &operator=(const HarmonySerialIODevice &) = delete;
    void setPortName(const QString &name);
    bool setBaudRate(qint32 baud);
    bool setDataBits(QSerialPort::DataBits bits);
    bool setParity(QSerialPort::Parity parity);
    bool setStopBits(QSerialPort::StopBits bits);
    bool setFlowControl(QSerialPort::FlowControl flow);
    bool open(QIODevice::OpenMode mode);
    void close();
    void cancel();
    bool isOpen() const;
    qint64 bytesAvailable() const;
    qint64 read(char *data, qint64 size);
    QByteArray readAll();
    qint64 write(const char *data, qint64 size);
    bool waitForReadyRead(int timeout);
    bool waitForBytesWritten(int timeout);
    bool flush();
    QSerialPort::SerialPortError error() const;
    QString errorString() const;
    void clearError();

private:
    struct State;
    bool _wait(const std::function<bool()> &predicate, int timeout);
    std::shared_ptr<State> _state;
};

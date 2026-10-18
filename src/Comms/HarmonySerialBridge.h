#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QMutex>
#include <QtCore/QObject>
#include <QtSerialPort/QSerialPort>
#include <atomic>
#include <functional>
#include <memory>

struct HarmonySerialPortInfo
{
    int portId = -1;
    QString deviceName{};
    bool hasUsbIdentity = false;
    int vendorId = -1;
    int productId = -1;
    QString manufacturer{};
    QString productName{};
    QString serialNumber{};

    QString portName() const { return QStringLiteral("harmony-serial:%1").arg(portId); }
};

struct HarmonySerialSettings
{
    qint32 baud = 57600;
    QSerialPort::DataBits dataBits = QSerialPort::Data8;
    QSerialPort::Parity parity = QSerialPort::NoParity;
    QSerialPort::StopBits stopBits = QSerialPort::OneStop;
    QSerialPort::FlowControl flowControl = QSerialPort::NoFlowControl;
    bool autoConnect = false;
    bool dtrForceLow = false;
};

class HarmonySerialProvider
{
public:
    enum class Operation
    {
        Open,
        Read,
        Write,
        SetBaud,
        Close
    };

    struct Request
    {
        Operation operation = Operation::Open;
        quint64 session = 0;
        QString portName;
        HarmonySerialSettings settings;
        QByteArray data;
    };

    struct Result
    {
        QByteArray data;
        qint64 written = 0;
        int errorCode = 0;
        QString error;
    };

    using Completion = std::function<void(Result)>;

    virtual ~HarmonySerialProvider() = default;
    virtual QList<HarmonySerialPortInfo> ports(QString* error, bool* factoryReady) = 0;
    virtual void call(const Request& request, Completion completion) = 0;
};

class HarmonySerialBridge : public QObject
{
    Q_OBJECT
public:
    explicit HarmonySerialBridge(QObject* parent = nullptr);
    ~HarmonySerialBridge() override;
    static QList<HarmonySerialPortInfo> availablePorts(QString* error = nullptr, bool* factoryReady = nullptr);
    static QList<qint32> supportedBaudRates();
    void open(const QString& portName, const HarmonySerialSettings& settings);
    void close();
    void write(const QByteArray& data);
    void setBaudRate(qint32 baud);

    bool isOpen() const { return _open.load(); }
#ifdef QGC_UNITTEST_BUILD
    static void setTestProvider(std::shared_ptr<HarmonySerialProvider> provider);
#endif

signals:
    void connected();
    void disconnected();
    void dataReceived(const QByteArray& data);
    void dataSent(const QByteArray& data);
    void baudRateChanged(qint32 baud);
    void errorOccurred(const QString& error);

private:
    struct Dispatch;
    using Result = HarmonySerialProvider::Result;
    using Request = HarmonySerialProvider::Request;
    void _request(const Request& request, std::function<void(Result)> completion);
    void _read();
    void _writeNext();
    void _fail(const QString& error);

    std::shared_ptr<HarmonySerialProvider> _provider;
    std::shared_ptr<Dispatch> _dispatch;
    quint64 _session = 0;
    std::atomic_bool _open = false;
    bool _opening = false;
    bool _writing = false;
    QByteArray _writes;
};

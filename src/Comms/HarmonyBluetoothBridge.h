#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <atomic>
#include <functional>
#include <memory>

struct HarmonyBluetoothDeviceInfo
{
    QString address;
    QString name;
    qint16 rssi = 0;
};

struct HarmonyBluetoothOptions
{
    bool lowEnergy = false;
    QString serviceUuid = QStringLiteral("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
    QString writeUuid = QStringLiteral("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
    QString notifyUuid = QStringLiteral("6e400003-b5a3-f393-e0a9-e50e24dcca9e");
};

class HarmonyBluetoothProvider
{
public:
    enum class Operation
    {
        StartScan,
        Scan,
        StopScan,
        Open,
        Read,
        Write,
        Close
    };

    struct Request
    {
        Operation operation = Operation::Open;
        quint64 session = 0;
        QString address;
        QByteArray data;
        HarmonyBluetoothOptions options;
    };

    struct Result
    {
        QList<HarmonyBluetoothDeviceInfo> devices;
        QByteArray data;
        qint64 written = 0;
        bool scanning = false;
        int errorCode = 0;
        QString error;
    };

    using Completion = std::function<void(Result)>;

    virtual ~HarmonyBluetoothProvider() = default;
    virtual bool supported(QString* error, bool* factoryReady) = 0;

    virtual bool transportSupported(bool lowEnergy, QString* error, bool* factoryReady)
    {
        Q_UNUSED(lowEnergy)
        return supported(error, factoryReady);
    }

    virtual void call(const Request& request, Completion completion) = 0;
};

class HarmonyBluetoothBridge : public QObject
{
    Q_OBJECT
public:
    explicit HarmonyBluetoothBridge(QObject* parent = nullptr);
    ~HarmonyBluetoothBridge() override;
    static bool isSupported(QString* error = nullptr, bool* factoryReady = nullptr);
    static bool isTransportSupported(bool lowEnergy, QString* error = nullptr, bool* factoryReady = nullptr);
    void startScan(const HarmonyBluetoothOptions& options = {});
    void stopScan();

    bool isScanning() const { return _scanning; }

    void open(const QString& address, const HarmonyBluetoothOptions& options = {});
    void close();
    void write(const QByteArray& data);

    bool isOpen() const { return _open.load(); }
#ifdef QGC_UNITTEST_BUILD
    static void setTestProvider(std::shared_ptr<HarmonyBluetoothProvider> provider);
#endif

signals:
    void deviceDiscovered(const QString& address, const QString& name, bool lowEnergy, qint16 rssi);
    void scanningChanged();
    void connected();
    void disconnected();
    void dataReceived(const QByteArray& data);
    void dataSent(const QByteArray& data);
    void errorOccurred(const QString& error);

private:
    struct Dispatch;
    using Result = HarmonyBluetoothProvider::Result;
    using Request = HarmonyBluetoothProvider::Request;
    void _request(const Request& request, std::function<void(Result)> completion);
    void _scan();
    void _scanResult(Result result);
    void _read();
    void _writeNext();
    void _fail(const QString& error);

    std::shared_ptr<HarmonyBluetoothProvider> _provider;
    std::shared_ptr<Dispatch> _dispatch;
    quint64 _session = 0;
    quint64 _scanSession = 0;
    bool _scanning = false;
    std::atomic_bool _open = false;
    bool _opening = false;
    bool _writing = false;
    HarmonyBluetoothOptions _scanOptions;
    HarmonyBluetoothOptions _socketOptions;
    qint64 _queuedWriteBytes = 0;
    QList<QByteArray> _writes;
};

#include "HarmonySerialBridge.h"

#include <QtCore/QMutexLocker>
#include <QtCore/QTimer>
#include <cstring>

#ifdef Q_OS_HARMONY
#include <QtCore/private/qcore_ohos_p.h>

#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY_ON(HarmonySerialBridgeLog, "Comms.HarmonySerialBridge")
#endif

namespace {
QMutex providerMutex;
std::atomic<quint64> nextSession = 0;
#ifdef QGC_UNITTEST_BUILD
std::shared_ptr<HarmonySerialProvider> testProvider;
#endif

QString resultError(const HarmonySerialProvider::Result& result)
{
    return result.errorCode ? QStringLiteral("%1 (%2)").arg(result.error).arg(result.errorCode) : result.error;
}

#ifdef Q_OS_HARMONY
HarmonySerialProvider::Result jsError(const Napi::Value& value)
{
    HarmonySerialProvider::Result result;
    result.error = QStringLiteral("USB serial operation failed");
    if (value.IsObject()) {
        const auto object = value.As<Napi::Object>();
        const auto code = object.Get("code");
        const auto message = object.Get("message");
        if (code.IsNumber()) {
            result.errorCode = code.As<Napi::Number>().Int32Value();
        }
        if (message.IsString()) {
            result.error = QString::fromStdString(message.As<Napi::String>().Utf8Value());
        }
    }
    return result;
}

int harmonyParity(QSerialPort::Parity parity)
{
    switch (parity) {
        case QSerialPort::NoParity:
            return 0;
        case QSerialPort::OddParity:
            return 1;
        case QSerialPort::EvenParity:
            return 2;
        case QSerialPort::MarkParity:
            return 3;
        case QSerialPort::SpaceParity:
            return 4;
        default:
            return -1;
    }
}

class NativeSerialProvider final : public HarmonySerialProvider
{
public:
    QList<HarmonySerialPortInfo> ports(QString* error, bool* factoryReady) override
    {
        struct Snapshot
        {
            QList<HarmonySerialPortInfo> ports;
            QString error;
            bool factoryReady = false;
            QString discoveryState;
            QString discoveryError;
        };

        const auto snapshot = QOhosJsThreadGateway::eval(
            [](QOhosJsState& jsState) {
                Snapshot snapshot;
                try {
                    const auto bridge = jsState.eval<QNapi::Object>("QGCSerialBridge");
                    if (!bridge.Get("getPorts").IsFunction()) {
                        snapshot.error = QStringLiteral("QGCSerialBridge factory has no getPorts function");
                        return snapshot;
                    }
                    snapshot.factoryReady = true;
                    const auto array = bridge.eval<QNapi::Array>("getPorts()");
                    for (uint32_t i = 0; i < array.Length(); ++i) {
                        const auto port = array.Get(i).As<Napi::Object>();
                        HarmonySerialPortInfo info;
                        info.portId = port.Get("portId").As<Napi::Number>().Int32Value();
                        info.deviceName = QString::fromStdString(port.Get("deviceName").As<Napi::String>().Utf8Value());
                        if (port.Get("hasUsbIdentity").IsBoolean() &&
                            port.Get("hasUsbIdentity").As<Napi::Boolean>().Value() && port.Get("vendorId").IsNumber() &&
                            port.Get("productId").IsNumber()) {
                            info.vendorId = port.Get("vendorId").As<Napi::Number>().Int32Value();
                            info.productId = port.Get("productId").As<Napi::Number>().Int32Value();
                            info.hasUsbIdentity = info.vendorId > 0 && info.vendorId <= 65535 && info.productId >= 0 &&
                                                  info.productId <= 65535;
                            if (port.Get("manufacturer").IsString()) {
                                info.manufacturer =
                                    QString::fromStdString(port.Get("manufacturer").As<Napi::String>().Utf8Value());
                            }
                            if (port.Get("productName").IsString()) {
                                info.productName =
                                    QString::fromStdString(port.Get("productName").As<Napi::String>().Utf8Value());
                            }
                            if (port.Get("serialNumber").IsString()) {
                                info.serialNumber =
                                    QString::fromStdString(port.Get("serialNumber").As<Napi::String>().Utf8Value())
                                        .trimmed();
                            }
                        }
                        snapshot.ports.append(std::move(info));
                    }
                } catch (const Napi::Error& exception) {
                    snapshot.error = resultError(jsError(exception.Value()));
                }
                if (snapshot.factoryReady) {
                    try {
                        const auto bridge = jsState.eval<QNapi::Object>("QGCSerialBridge");
                        if (bridge.Get("getDiscoveryState").IsFunction()) {
                            snapshot.discoveryState =
                                QString::fromStdString(bridge.eval<QNapi::String>("getDiscoveryState()").Utf8Value());
                        } else {
                            snapshot.discoveryError =
                                QStringLiteral("QGCSerialBridge factory has no getDiscoveryState function");
                        }
                    } catch (const Napi::Error& exception) {
                        snapshot.discoveryError = resultError(jsError(exception.Value()));
                    }
                }
                return snapshot;
            },
            Q_FUNC_INFO);
        QString discoveryState = snapshot.discoveryState;
        if (discoveryState.isEmpty()) {
            discoveryState = QStringLiteral("factoryReady=%1 portCount=%2 discoveryState=unavailable")
                                 .arg(snapshot.factoryReady)
                                 .arg(snapshot.ports.size());
        }
        if (!snapshot.error.isEmpty()) {
            discoveryState += QStringLiteral(" error=%1").arg(snapshot.error);
        }
        if (!snapshot.discoveryError.isEmpty()) {
            discoveryState += QStringLiteral(" discoveryStateError=%1").arg(snapshot.discoveryError);
        }
        static QMutex discoveryMutex;
        static QString lastDiscoveryState;
        bool discoveryChanged;
        {
            const QMutexLocker locker(&discoveryMutex);
            discoveryChanged = discoveryState != lastDiscoveryState;
            lastDiscoveryState = discoveryState;
        }
        if (discoveryChanged) {
            qCInfo(HarmonySerialBridgeLog).noquote()
                << QStringLiteral("QGC USB serial discovery: %1").arg(discoveryState);
        }
        if (error) {
            *error = snapshot.error;
        }
        if (factoryReady) {
            *factoryReady = snapshot.factoryReady;
        }
        return snapshot.ports;
    }

    void call(const Request& request, Completion completion) override
    {
        QOhosJsThreadGateway::invoke([request, completion = std::move(completion)](QOhosJsState& jsState) {
            try {
                std::vector<QNapi::ValueWrapper> arguments{static_cast<double>(request.session)};
                std::string method;
                switch (request.operation) {
                    case Operation::Open:
                        method = "open(*)";
                        arguments.emplace_back(request.portName.toStdString());
                        arguments.emplace_back(request.settings.baud);
                        arguments.emplace_back(static_cast<int>(request.settings.dataBits));
                        arguments.emplace_back(harmonyParity(request.settings.parity));
                        arguments.emplace_back(request.settings.stopBits == QSerialPort::OneStop ? 0 : 1);
                        arguments.emplace_back(request.settings.autoConnect);
                        arguments.emplace_back(request.settings.dtrForceLow);
                        break;
                    case Operation::Read:
                        method = "read(*)";
                        break;
                    case Operation::Write: {
                        method = "write(*)";
                        auto buffer = Napi::Uint8Array::New(jsState.env(), request.data.size());
                        std::memcpy(buffer.Data(), request.data.constData(), request.data.size());
                        arguments.emplace_back(buffer);
                        break;
                    }
                    case Operation::SetBaud:
                        method = "setBaud(*)";
                        arguments.emplace_back(request.settings.baud);
                        break;
                    case Operation::Close:
                        jsState.eval<QNapi::Value>("QGCSerialBridge.close(*)", arguments);
                        completion({});
                        return;
                }
                jsState.evalToPromiseOrRejectOnThrow("QGCSerialBridge." + method, arguments)
                    .onThen(
                        [request, completion](const Napi::CallbackInfo& info) {
                            Result result;
                            if (request.operation == Operation::Read) {
                                const auto buffer = info[0].As<Napi::Uint8Array>();
                                if (buffer.ByteLength() > 8192) {
                                    result.error = QStringLiteral("USB serial read exceeded the receive buffer");
                                } else {
                                    result.data =
                                        QByteArray(reinterpret_cast<const char*>(buffer.Data()), buffer.ByteLength());
                                }
                            } else if (request.operation == Operation::Write) {
                                result.written = info[0].As<Napi::Number>().Int64Value();
                            }
                            completion(std::move(result));
                        },
                        [completion](const Napi::CallbackInfo& info) { completion(jsError(info[0])); });
            } catch (const Napi::Error& exception) {
                completion(jsError(exception.Value()));
            }
        });
    }
};
#else
class NativeSerialProvider final : public HarmonySerialProvider
{
public:
    QList<HarmonySerialPortInfo> ports(QString* error, bool* factoryReady) override
    {
        if (error) {
            *error = QStringLiteral("Harmony USB serial capability is unavailable");
        }
        if (factoryReady) {
            *factoryReady = false;
        }
        return {};
    }

    void call(const Request&, Completion completion) override
    {
        Result result;
        result.error = QStringLiteral("Harmony USB serial capability is unavailable");
        completion(std::move(result));
    }
};
#endif

std::shared_ptr<HarmonySerialProvider> provider()
{
    const QMutexLocker locker(&providerMutex);
#ifdef QGC_UNITTEST_BUILD
    if (testProvider) {
        return testProvider;
    }
#endif
    static const auto nativeProvider = std::make_shared<NativeSerialProvider>();
    return nativeProvider;
}
}  // namespace

struct HarmonySerialBridge::Dispatch
{
    QMutex mutex;
    HarmonySerialBridge* target = nullptr;
};

HarmonySerialBridge::HarmonySerialBridge(QObject* parent)
    : QObject(parent), _provider(provider()), _dispatch(std::make_shared<Dispatch>())
{
    _dispatch->target = this;
}

HarmonySerialBridge::~HarmonySerialBridge()
{
    close();
    const QMutexLocker locker(&_dispatch->mutex);
    _dispatch->target = nullptr;
}

QList<HarmonySerialPortInfo> HarmonySerialBridge::availablePorts(QString* error, bool* factoryReady)
{
    return provider()->ports(error, factoryReady);
}

QList<qint32> HarmonySerialBridge::supportedBaudRates()
{
    return {50,     75,     110,     134,     150,     200,     300,     600,     1200,    1800,   2400,   4800,
            9600,   14400,  19200,   38400,   56000,   57600,   115200,  128000,  230400,  256000, 460800, 500000,
            576000, 921600, 1000000, 1152000, 1500000, 2000000, 2500000, 3000000, 3500000, 4000000};
}

void HarmonySerialBridge::_request(const Request& request, std::function<void(Result)> completion)
{
    const std::weak_ptr<Dispatch> weakDispatch = _dispatch;
    _provider->call(request,
                    [weakDispatch, session = request.session, completion = std::move(completion)](Result result) {
                        const auto dispatch = weakDispatch.lock();
                        if (!dispatch) {
                            return;
                        }
                        const QMutexLocker locker(&dispatch->mutex);
                        if (!dispatch->target) {
                            return;
                        }
                        QMetaObject::invokeMethod(
                            dispatch->target,
                            [dispatch, session, completion, result = std::move(result)]() mutable {
                                HarmonySerialBridge* const target = dispatch->target;
                                if (target && target->_session == session) {
                                    completion(std::move(result));
                                }
                            },
                            Qt::QueuedConnection);
                    });
}

void HarmonySerialBridge::open(const QString& portName, const HarmonySerialSettings& settings)
{
    if (_open || _opening) {
        return;
    }
    if (settings.flowControl != QSerialPort::NoFlowControl) {
        _fail(tr("Harmony USB serial does not support flow control"));
        return;
    }
    if (settings.stopBits != QSerialPort::OneStop && settings.stopBits != QSerialPort::TwoStop) {
        _fail(tr("Harmony USB serial does not support the selected stop bits"));
        return;
    }
    if (!supportedBaudRates().contains(settings.baud) || settings.dataBits < QSerialPort::Data5 ||
        settings.dataBits > QSerialPort::Data8 ||
        (settings.parity != QSerialPort::NoParity && settings.parity != QSerialPort::OddParity &&
         settings.parity != QSerialPort::EvenParity && settings.parity != QSerialPort::MarkParity &&
         settings.parity != QSerialPort::SpaceParity)) {
        _fail(tr("Harmony USB serial does not support the selected serial parameters"));
        return;
    }
    _session = ++nextSession;
    _opening = true;
    Request request;
    request.operation = HarmonySerialProvider::Operation::Open;
    request.session = _session;
    request.portName = portName;
    request.settings = settings;
    _request(request, [this](Result result) {
        if (!result.error.isEmpty() || result.errorCode) {
            _fail(resultError(result));
            return;
        }
        _opening = false;
        _open = true;
        emit connected();
        _read();
    });
}

void HarmonySerialBridge::close()
{
    if (!_session) {
        return;
    }
    Request request;
    request.operation = HarmonySerialProvider::Operation::Close;
    request.session = _session;
    _session = 0;
    _opening = false;
    _open = false;
    _writing = false;
    _writes.clear();
    _provider->call(request, [](Result) {});
    emit disconnected();
}

void HarmonySerialBridge::_fail(const QString& error)
{
    emit errorOccurred(error);
    if (_session) {
        close();
    } else {
        emit disconnected();
    }
}

void HarmonySerialBridge::_read()
{
    if (!_open) {
        return;
    }
    Request request;
    request.operation = HarmonySerialProvider::Operation::Read;
    request.session = _session;
    _request(request, [this, session = _session](Result result) {
        if (!result.error.isEmpty() || result.errorCode) {
            _fail(resultError(result));
            return;
        }
        if (result.data.size() > 8192) {
            _fail(tr("USB serial read exceeded the receive buffer"));
            return;
        }
        if (!result.data.isEmpty()) {
            emit dataReceived(result.data);
        }
        QTimer::singleShot(result.data.isEmpty() ? 5 : 0, this, [this, session]() {
            if (_session == session) {
                _read();
            }
        });
    });
}

void HarmonySerialBridge::write(const QByteArray& data)
{
    if (data.isEmpty()) {
        emit errorOccurred(tr("Data to Send is Empty"));
        return;
    }
    if (!_open) {
        emit errorOccurred(tr("Port is not Connected"));
        return;
    }
    _writes.append(data);
    if (!_writing) {
        _writeNext();
    }
}

void HarmonySerialBridge::setBaudRate(qint32 baud)
{
    if (!_open) {
        emit errorOccurred(tr("Port is not Connected"));
        return;
    }
    if (!supportedBaudRates().contains(baud)) {
        emit errorOccurred(tr("Harmony USB serial does not support the selected baud rate"));
        return;
    }
    Request request;
    request.operation = HarmonySerialProvider::Operation::SetBaud;
    request.session = _session;
    request.settings.baud = baud;
    _request(request, [this, baud](Result result) {
        if (!result.error.isEmpty() || result.errorCode) {
            _fail(resultError(result));
            return;
        }
        emit baudRateChanged(baud);
    });
}

void HarmonySerialBridge::_writeNext()
{
    if (!_open || _writes.isEmpty()) {
        _writing = false;
        return;
    }
    _writing = true;
    Request request;
    request.operation = HarmonySerialProvider::Operation::Write;
    request.session = _session;
    request.data = _writes.left(4096);
    _request(request, [this, data = request.data](Result result) {
        if (!result.error.isEmpty() || result.errorCode) {
            _fail(resultError(result));
            return;
        }
        if (result.written == 0) {
            QTimer::singleShot(25, this, [this, session = _session]() {
                if (_session == session) {
                    _writeNext();
                }
            });
            return;
        }
        if (result.written < 0 || result.written > data.size()) {
            _fail(tr("Could Not Send Data - Write Returned an Invalid Byte Count"));
            return;
        }
        _writes.remove(0, result.written);
        emit dataSent(data.first(result.written));
        _writeNext();
    });
}

#ifdef QGC_UNITTEST_BUILD
void HarmonySerialBridge::setTestProvider(std::shared_ptr<HarmonySerialProvider> test)
{
    const QMutexLocker locker(&providerMutex);
    testProvider = std::move(test);
}
#endif

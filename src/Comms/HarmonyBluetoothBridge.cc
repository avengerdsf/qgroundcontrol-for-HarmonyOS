#include "HarmonyBluetoothBridge.h"

#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QTimer>
#include <cstring>

#ifdef Q_OS_HARMONY
#include <QtCore/private/qcore_ohos_p.h>
#endif

namespace {
QMutex providerMutex;
std::atomic<quint64> nextSession = 0;
#ifdef QGC_UNITTEST_BUILD
std::shared_ptr<HarmonyBluetoothProvider> testProvider;
#endif

QString resultError(const HarmonyBluetoothProvider::Result& result)
{
    return result.errorCode ? QStringLiteral("%1 (%2)").arg(result.error).arg(result.errorCode) : result.error;
}

#ifdef Q_OS_HARMONY
HarmonyBluetoothProvider::Result jsError(const Napi::Value& value)
{
    HarmonyBluetoothProvider::Result result;
    result.error = QStringLiteral("Bluetooth operation failed");
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

HarmonyBluetoothProvider::Result discoveryResult(const Napi::Value& value)
{
    HarmonyBluetoothProvider::Result result;
    const auto snapshot = value.As<Napi::Object>();
    result.scanning = snapshot.Get("scanning").As<Napi::Boolean>().Value();
    const auto devices = snapshot.Get("devices").As<Napi::Array>();
    for (uint32_t i = 0; i < devices.Length(); ++i) {
        const auto device = devices.Get(i).As<Napi::Object>();
        result.devices.append({QString::fromStdString(device.Get("address").As<Napi::String>().Utf8Value()),
                               QString::fromStdString(device.Get("name").As<Napi::String>().Utf8Value()),
                               device.Get("rssi").IsNumber()
                                   ? static_cast<qint16>(device.Get("rssi").As<Napi::Number>().Int32Value())
                                   : qint16(0)});
    }
    return result;
}

class NativeBluetoothProvider final : public HarmonyBluetoothProvider
{
public:
    bool supported(QString* error, bool* factoryReady) override
    {
        QString classicError;
        QString bleError;
        bool classicReady = false;
        bool bleReady = false;
        const bool classic = transportSupported(false, &classicError, &classicReady);
        const bool ble = transportSupported(true, &bleError, &bleReady);
        if (factoryReady) {
            *factoryReady = classicReady || bleReady;
        }
        if (error) {
            *error = (classic || ble) ? QString() : classicError + QStringLiteral("; ") + bleError;
        }
        return classic || ble;
    }

    bool transportSupported(bool lowEnergy, QString* error, bool* factoryReady) override
    {
        struct Snapshot
        {
            bool supported = false;
            bool factoryReady = false;
            QString error;
        };

        const auto snapshot = QOhosJsThreadGateway::eval(
            [lowEnergy](QOhosJsState& jsState) {
                Snapshot snapshot;
                try {
                    const char* name = lowEnergy ? "QGCBleBridge" : "QGCBluetoothBridge";
                    {
                        const auto bridge = jsState.eval<QNapi::Object>(name);
                        for (const char* method :
                             {"isSupported", "startScan", "scan", "stopScan", "open", "read", "write", "close"}) {
                            if (!bridge.Get(method).IsFunction()) {
                                snapshot.error = QStringLiteral("%1 factory has no %2 function")
                                                     .arg(QString::fromLatin1(name), QString::fromLatin1(method));
                                return snapshot;
                            }
                        }
                        snapshot.supported = bridge.eval<QNapi::Boolean>("isSupported()").Value();
                    }
                    snapshot.factoryReady = true;
                    if (!snapshot.supported) {
                        snapshot.error = QStringLiteral("Bluetooth capability is unavailable (801)");
                    }
                } catch (const Napi::Error& exception) {
                    snapshot.error = resultError(jsError(exception.Value()));
                }
                return snapshot;
            },
            Q_FUNC_INFO);
        if (error) {
            *error = snapshot.error;
        }
        if (factoryReady) {
            *factoryReady = snapshot.factoryReady;
        }
        return snapshot.supported;
    }

    void call(const Request& request, Completion completion) override
    {
        QOhosJsThreadGateway::invoke([request, completion = std::move(completion)](QOhosJsState& jsState) {
            try {
                const std::string bridgeName = request.options.lowEnergy ? "QGCBleBridge" : "QGCBluetoothBridge";
                std::vector<QNapi::ValueWrapper> arguments{static_cast<double>(request.session)};
                std::string method;
                switch (request.operation) {
                    case Operation::StartScan:
                    case Operation::Open: {
                        const auto ability = jsState.defaultQAbility();
                        if (!ability || !ability->Get("context").IsObject()) {
                            Result result;
                            result.error = QStringLiteral("Bluetooth authorization requires a UIAbility context");
                            completion(std::move(result));
                            return;
                        }
                        method = request.operation == Operation::StartScan ? "startScan(*)" : "open(*)";
                        if (request.operation == Operation::Open) {
                            arguments.emplace_back(request.address.toStdString());
                        }
                        arguments.emplace_back(ability->Get("context"));
                        if (request.operation == Operation::Open && request.options.lowEnergy) {
                            auto options = Napi::Object::New(jsState.env());
                            options.Set("serviceUuid", request.options.serviceUuid.toStdString());
                            options.Set("writeUuid", request.options.writeUuid.toStdString());
                            options.Set("notifyUuid", request.options.notifyUuid.toStdString());
                            arguments.emplace_back(options);
                        }
                        break;
                    }
                    case Operation::Scan:
                        completion(discoveryResult(jsState.eval<QNapi::Object>(bridgeName + ".scan(*)", arguments)));
                        return;
                    case Operation::StopScan:
                        jsState.eval<QNapi::Value>(bridgeName + ".stopScan(*)", arguments);
                        completion({});
                        return;
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
                    case Operation::Close:
                        jsState.eval<QNapi::Value>(bridgeName + ".close(*)", arguments);
                        completion({});
                        return;
                }
                jsState.evalToPromiseOrRejectOnThrow(bridgeName + "." + method, arguments)
                    .onThen(
                        [request, completion](const Napi::CallbackInfo& info) {
                            try {
                                Result result;
                                if (request.operation == Operation::StartScan) {
                                    result = discoveryResult(info[0]);
                                } else if (request.operation == Operation::Read) {
                                    const auto buffer = info[0].As<Napi::Uint8Array>();
                                    if (buffer.ByteLength() > 8192) {
                                        result.error = QStringLiteral("Bluetooth read exceeded the receive buffer");
                                    } else {
                                        result.data = QByteArray(reinterpret_cast<const char*>(buffer.Data()),
                                                                 buffer.ByteLength());
                                    }
                                } else if (request.operation == Operation::Write) {
                                    result.written = info[0].As<Napi::Number>().Int64Value();
                                }
                                completion(std::move(result));
                            } catch (const Napi::Error& exception) {
                                completion(jsError(exception.Value()));
                            }
                        },
                        [completion](const Napi::CallbackInfo& info) { completion(jsError(info[0])); });
            } catch (const Napi::Error& exception) {
                completion(jsError(exception.Value()));
            }
        });
    }
};
#else
class NativeBluetoothProvider final : public HarmonyBluetoothProvider
{
public:
    bool supported(QString* error, bool* factoryReady) override
    {
        if (error) {
            *error = QStringLiteral("Harmony Bluetooth capability is unavailable (801)");
        }
        if (factoryReady) {
            *factoryReady = false;
        }
        return false;
    }

    void call(const Request&, Completion completion) override
    {
        Result result;
        result.errorCode = 801;
        result.error = QStringLiteral("Harmony Bluetooth capability is unavailable");
        completion(std::move(result));
    }
};
#endif

std::shared_ptr<HarmonyBluetoothProvider> provider()
{
    const QMutexLocker locker(&providerMutex);
#ifdef QGC_UNITTEST_BUILD
    if (testProvider) {
        return testProvider;
    }
#endif
    static const auto nativeProvider = std::make_shared<NativeBluetoothProvider>();
    return nativeProvider;
}
}  // namespace

struct HarmonyBluetoothBridge::Dispatch
{
    QMutex mutex;
    HarmonyBluetoothBridge* target = nullptr;
};

HarmonyBluetoothBridge::HarmonyBluetoothBridge(QObject* parent)
    : QObject(parent), _provider(provider()), _dispatch(std::make_shared<Dispatch>())
{
    _dispatch->target = this;
}

HarmonyBluetoothBridge::~HarmonyBluetoothBridge()
{
    stopScan();
    close();
    const QMutexLocker locker(&_dispatch->mutex);
    _dispatch->target = nullptr;
}

bool HarmonyBluetoothBridge::isSupported(QString* error, bool* factoryReady)
{
    return provider()->supported(error, factoryReady);
}

bool HarmonyBluetoothBridge::isTransportSupported(bool lowEnergy, QString* error, bool* factoryReady)
{
    return provider()->transportSupported(lowEnergy, error, factoryReady);
}

void HarmonyBluetoothBridge::_request(const Request& request, std::function<void(Result)> completion)
{
    const std::weak_ptr<Dispatch> weakDispatch = _dispatch;
    const bool discovery = request.operation == HarmonyBluetoothProvider::Operation::StartScan ||
                           request.operation == HarmonyBluetoothProvider::Operation::Scan;
    Request effectiveRequest = request;
    effectiveRequest.options = discovery ? _scanOptions : _socketOptions;
    _provider->call(effectiveRequest, [weakDispatch, discovery, session = request.session,
                                       completion = std::move(completion)](Result result) {
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
            [dispatch, discovery, session, completion, result = std::move(result)]() mutable {
                HarmonyBluetoothBridge* const target = dispatch->target;
                if (target && (discovery ? target->_scanSession : target->_session) == session) {
                    completion(std::move(result));
                }
            },
            Qt::QueuedConnection);
    });
}

void HarmonyBluetoothBridge::startScan(const HarmonyBluetoothOptions& options)
{
    if (_scanning) {
        return;
    }
    _scanOptions = options;
    _scanSession = ++nextSession;
    _scanning = true;
    emit scanningChanged();
    Request request;
    request.operation = HarmonyBluetoothProvider::Operation::StartScan;
    request.session = _scanSession;
    _request(request, [this](Result result) { _scanResult(std::move(result)); });
}

void HarmonyBluetoothBridge::stopScan()
{
    if (!_scanSession) {
        return;
    }
    Request request;
    request.operation = HarmonyBluetoothProvider::Operation::StopScan;
    request.session = _scanSession;
    request.options = _scanOptions;
    _scanSession = 0;
    _scanning = false;
    _provider->call(request, [](Result) {});
    emit scanningChanged();
}

void HarmonyBluetoothBridge::_scan()
{
    if (!_scanning) {
        return;
    }
    Request request;
    request.operation = HarmonyBluetoothProvider::Operation::Scan;
    request.session = _scanSession;
    _request(request, [this](Result result) { _scanResult(std::move(result)); });
}

void HarmonyBluetoothBridge::_scanResult(Result result)
{
    if (!result.error.isEmpty() || result.errorCode) {
        emit errorOccurred(resultError(result));
        stopScan();
        return;
    }
    for (const auto& device : result.devices) {
        emit deviceDiscovered(device.address, device.name, _scanOptions.lowEnergy, device.rssi);
    }
    if (!result.scanning) {
        stopScan();
        return;
    }
    QTimer::singleShot(250, this, [this, session = _scanSession]() {
        if (_scanSession == session) {
            _scan();
        }
    });
}

void HarmonyBluetoothBridge::open(const QString& address, const HarmonyBluetoothOptions& options)
{
    if (_open || _opening) {
        return;
    }
    if (address.isEmpty()) {
        _fail(tr("Bluetooth device address is empty"));
        return;
    }
    _socketOptions = options;
    _session = ++nextSession;
    _opening = true;
    Request request;
    request.operation = HarmonyBluetoothProvider::Operation::Open;
    request.session = _session;
    request.address = address;
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

void HarmonyBluetoothBridge::close()
{
    if (!_session) {
        return;
    }
    Request request;
    request.operation = HarmonyBluetoothProvider::Operation::Close;
    request.session = _session;
    request.options = _socketOptions;
    _session = 0;
    _opening = false;
    _open = false;
    _writing = false;
    _writes.clear();
    _queuedWriteBytes = 0;
    _provider->call(request, [](Result) {});
    emit disconnected();
}

void HarmonyBluetoothBridge::_fail(const QString& error)
{
    emit errorOccurred(error);
    if (_session) {
        close();
    } else {
        emit disconnected();
    }
}

void HarmonyBluetoothBridge::_read()
{
    if (!_open) {
        return;
    }
    Request request;
    request.operation = HarmonyBluetoothProvider::Operation::Read;
    request.session = _session;
    _request(request, [this, session = _session](Result result) {
        if (!result.error.isEmpty() || result.errorCode) {
            _fail(resultError(result));
            return;
        }
        if (result.data.size() > 8192) {
            _fail(tr("Bluetooth read exceeded the receive buffer"));
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

void HarmonyBluetoothBridge::write(const QByteArray& data)
{
    if (data.isEmpty()) {
        emit errorOccurred(tr("Data to Send is Empty"));
        return;
    }
    if (!_open) {
        emit errorOccurred(tr("Socket is not connected"));
        return;
    }
    static constexpr qint64 transmitCapacity = 256 * 1024;
    if (data.size() > transmitCapacity - _queuedWriteBytes) {
        _fail(tr("Bluetooth transmit queue exceeded its capacity"));
        return;
    }
    _writes.append(data);
    _queuedWriteBytes += data.size();
    if (!_writing) {
        _writeNext();
    }
}

void HarmonyBluetoothBridge::_writeNext()
{
    if (!_open || _writes.isEmpty()) {
        _writing = false;
        return;
    }
    _writing = true;
    Request request;
    request.operation = HarmonyBluetoothProvider::Operation::Write;
    request.session = _session;
    request.data = _writes.first().left(4096);
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
        _writes.first().remove(0, result.written);
        _queuedWriteBytes -= result.written;
        if (_writes.first().isEmpty()) {
            _writes.removeFirst();
        }
        emit dataSent(data.first(result.written));
        _writeNext();
    });
}

#ifdef QGC_UNITTEST_BUILD
void HarmonyBluetoothBridge::setTestProvider(std::shared_ptr<HarmonyBluetoothProvider> provider)
{
    const QMutexLocker locker(&providerMutex);
    testProvider = std::move(provider);
}
#endif

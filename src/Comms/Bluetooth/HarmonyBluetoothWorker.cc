#include "HarmonyBluetoothWorker.h"

HarmonyBluetoothWorker::HarmonyBluetoothWorker(const BluetoothConfiguration* config, QObject* parent)
    : BluetoothWorker(config, parent), _options(config->harmonyOptions())
{}

HarmonyBluetoothWorker::~HarmonyBluetoothWorker()
{
    _intentionalDisconnect = true;
    if (_bridge) {
        _bridge->disconnect(this);
        _bridge->close();
    }
}

void HarmonyBluetoothWorker::onSetupConnection()
{
    if (_bridge) {
        return;
    }
    _bridge = new HarmonyBluetoothBridge(this);
    connect(_bridge, &HarmonyBluetoothBridge::connected, this, [this]() {
        _connected = true;
        _consecutiveFailures = 0;
        _reconnectAttempts = 0;
        _reconnectTimer->stop();
        _reconnectTimer->setInterval(RECONNECT_BASE_INTERVAL_MS);
        emit connected();
    });
    connect(_bridge, &HarmonyBluetoothBridge::disconnected, this, [this]() {
        const bool wasConnected = _connected.exchange(false);
        emit disconnected();
        if (wasConnected && !_intentionalDisconnect.load()) {
            _reconnectTimer->start();
        }
    });
    connect(_bridge, &HarmonyBluetoothBridge::errorOccurred, this, [this](const QString& error) {
        ++_consecutiveFailures;
        emit errorOccurred(error);
    });
    connect(_bridge, &HarmonyBluetoothBridge::dataReceived, this, &BluetoothWorker::dataReceived);
    connect(_bridge, &HarmonyBluetoothBridge::dataSent, this, &BluetoothWorker::dataSent);
}

void HarmonyBluetoothWorker::onConnectLink()
{
    QString error;
    if (!HarmonyBluetoothBridge::isTransportSupported(_options.lowEnergy, &error)) {
        emit errorOccurred(error);
        emit disconnected();
        return;
    }
    onSetupConnection();
    _bridge->open(_device.address().isNull() ? QString() : _device.address().toString(), _options);
}

void HarmonyBluetoothWorker::onDisconnectLink()
{
    if (_bridge) {
        _bridge->close();
    }
}

void HarmonyBluetoothWorker::onWriteData(const QByteArray& data)
{
    if (_bridge) {
        _bridge->write(data);
    }
}

void HarmonyBluetoothWorker::onServiceDiscoveryTimeout()
{
    onDisconnectLink();
}

void HarmonyBluetoothWorker::onResetAfterConsecutiveFailures()
{
    onDisconnectLink();
}

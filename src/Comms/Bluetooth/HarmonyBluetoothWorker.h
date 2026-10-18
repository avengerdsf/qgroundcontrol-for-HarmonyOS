#pragma once

#include "BluetoothWorker.h"
#include "HarmonyBluetoothBridge.h"

class HarmonyBluetoothWorker : public BluetoothWorker
{
    Q_OBJECT

public:
    explicit HarmonyBluetoothWorker(const BluetoothConfiguration* config, QObject* parent = nullptr);
    ~HarmonyBluetoothWorker() override;

protected:
    void onSetupConnection() override;
    void onConnectLink() override;
    void onDisconnectLink() override;
    void onWriteData(const QByteArray& data) override;
    void onServiceDiscoveryTimeout() override;
    void onResetAfterConsecutiveFailures() override;

private:
    const HarmonyBluetoothOptions _options;
    QPointer<HarmonyBluetoothBridge> _bridge;
};

#pragma once

#include <QtCore/QMap>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <atomic>
#include <memory>

struct SDL_Joystick;
class Vehicle;
class HarmonyJoystickInputTestHelper;
struct GameDevice_DeviceEvent;
struct GamePad_AxisEvent;
struct GamePad_ButtonEvent;

class HarmonyJoystickInput : public QObject
{
    Q_OBJECT
public:
    enum class Status
    {
        Stopped,
        Unsupported,
        Monitoring,
        RegistrationFailed
    };

    explicit HarmonyJoystickInput(QObject* parent = nullptr);
    ~HarmonyJoystickInput() override;
    Status start();
    void stop();
    void setFocused(bool focused);

    Status status() const { return _status; }

#ifdef QGC_UNITTEST_BUILD
    static bool allowsVehicleControl(const QString& name, Vehicle* vehicle);
#endif

signals:
    void devicesChanged();

private:
#if defined(QGC_UNITTEST_BUILD) || defined(QGC_HARMONY_JOYSTICK_NATIVE_TEST)
    friend class HarmonyJoystickInputTestHelper;
    bool _simulation = false;
#endif
    struct Device
    {
        SDL_Joystick* joystick = nullptr;
        quint32 instanceId = 0;
    };

    quint32 _deviceAdded(const QString& id, const QString& name);
    void _deviceRemoved(const QString& id);
    void _axis(const QString& id, int axis, double value);
    void _button(const QString& id, int keyCode, bool down);
    void _clearDevices();
    void _resetValues(Device& device);

#ifdef Q_OS_HARMONY
    struct NativeApi;
    std::unique_ptr<NativeApi> _nativeApi;
    static void _onDevice(const GameDevice_DeviceEvent* event);
    static void _onAxis(const GamePad_AxisEvent* event);
    static void _onButton(const GamePad_ButtonEvent* event);
#endif

    QMap<QString, Device> _devices;
    Status _status = Status::Stopped;
    bool _focused = true;
    std::atomic<quint64> _epoch = 0;
    std::atomic<quint64> _lifetime = 0;
};

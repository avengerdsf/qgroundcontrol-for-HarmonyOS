#include "HarmonyJoystickInput.h"

#include <SDL.h>
#include <QtCore/QDebug>
#include <algorithm>
#include <cmath>

#ifdef Q_OS_HARMONY
#include <GameControllerKit/game_device.h>
#include <GameControllerKit/game_pad.h>
#include <syscap_ndk.h>
#include <QtCore/QLibrary>
#include <QtCore/QMetaObject>
#include <cstdlib>
#include <mutex>
#include <vector>

// The SDK's deviceinfo.h collides with QGC's DeviceInfo.h on Windows hosts.
extern "C" const char *OH_GetProductModel(void);
#endif

#if defined(QGC_UNITTEST_BUILD) && !defined(QGC_HARMONY_JOYSTICK_NATIVE_TEST)
#include "Vehicle.h"
#include "VehicleLinkManager.h"
#include "LinkInterface.h"
#include "LinkConfiguration.h"
#endif

namespace {
constexpr const char *testPrefix = "QGC Test Gamepad ";

int buttonIndex(int keyCode)
{
    switch (keyCode) {
    case 2301: return SDL_CONTROLLER_BUTTON_A;
    case 2302: return SDL_CONTROLLER_BUTTON_B;
    case 2304: return SDL_CONTROLLER_BUTTON_X;
    case 2305: return SDL_CONTROLLER_BUTTON_Y;
    case 2307: return SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
    case 2308: return SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
    case 2311: return SDL_CONTROLLER_BUTTON_BACK;
    case 2312: return SDL_CONTROLLER_BUTTON_START;
    case 2314: return SDL_CONTROLLER_BUTTON_LEFTSTICK;
    case 2315: return SDL_CONTROLLER_BUTTON_RIGHTSTICK;
    case 2012: return SDL_CONTROLLER_BUTTON_DPAD_UP;
    case 2013: return SDL_CONTROLLER_BUTTON_DPAD_DOWN;
    case 2014: return SDL_CONTROLLER_BUTTON_DPAD_LEFT;
    case 2015: return SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
    default: return -1;
    }
}

#ifdef Q_OS_HARMONY
std::mutex callbackMutex;
HarmonyJoystickInput *callbackInput = nullptr;
#endif
}

#ifdef Q_OS_HARMONY
#define GAME_API_FUNCTIONS(X) \
    X(GameDevice_RegisterDeviceMonitor) X(GameDevice_UnregisterDeviceMonitor) \
    X(GameDevice_GetAllDeviceInfos) X(GameDevice_AllDeviceInfos_GetCount) \
    X(GameDevice_AllDeviceInfos_GetDeviceInfo) X(GameDevice_DestroyAllDeviceInfos) \
    X(GameDevice_DeviceEvent_GetChangedType) X(GameDevice_DeviceEvent_GetDeviceInfo) \
    X(GameDevice_DeviceInfo_GetDeviceId) X(GameDevice_DeviceInfo_GetName) \
    X(GameDevice_DeviceInfo_GetDeviceType) X(GameDevice_DestroyDeviceInfo) \
    X(GamePad_AxisEvent_GetDeviceId) X(GamePad_AxisEvent_GetAxisSourceType) \
    X(GamePad_AxisEvent_GetXAxisValue) X(GamePad_AxisEvent_GetYAxisValue) \
    X(GamePad_AxisEvent_GetZAxisValue) X(GamePad_AxisEvent_GetRZAxisValue) \
    X(GamePad_AxisEvent_GetHatXAxisValue) X(GamePad_AxisEvent_GetHatYAxisValue) \
    X(GamePad_AxisEvent_GetBrakeAxisValue) X(GamePad_AxisEvent_GetGasAxisValue) \
    X(GamePad_ButtonEvent_GetDeviceId) X(GamePad_ButtonEvent_GetButtonAction) X(GamePad_ButtonEvent_GetButtonCode)

struct HarmonyJoystickInput::NativeApi {
    QLibrary library{QStringLiteral("libohgame_controller.z.so")};
#define DECLARE_FUNCTION(name) decltype(&OH_##name) name = nullptr;
    GAME_API_FUNCTIONS(DECLARE_FUNCTION)
#undef DECLARE_FUNCTION
    using AxisRegister = GameController_ErrorCode (*)(GamePad_AxisInputMonitorCallback);
    using ButtonRegister = GameController_ErrorCode (*)(GamePad_ButtonInputMonitorCallback);
    using Unregister = GameController_ErrorCode (*)();
    std::vector<Unregister> registered;

    template<typename T> bool resolve(T &function, const char *name) {
        function = reinterpret_cast<T>(library.resolve(name));
        return function != nullptr;
    }
    bool load() {
        if (!library.load()) return false;
#define RESOLVE_FUNCTION(name) if (!resolve(name, "OH_" #name)) return false;
        GAME_API_FUNCTIONS(RESOLVE_FUNCTION)
#undef RESOLVE_FUNCTION
        return true;
    }
    bool registerInputs() {
        const auto deviceResult = GameDevice_RegisterDeviceMonitor(&HarmonyJoystickInput::_onDevice);
        if (deviceResult != GAME_CONTROLLER_SUCCESS) {
            qInfo() << "Harmony GameController RegistrationFailed: device monitor" << static_cast<int>(deviceResult);
            return false;
        }
        registered.push_back(GameDevice_UnregisterDeviceMonitor);
        const char *axes[] = {"LeftThumbstick", "RightThumbstick", "LeftTrigger", "RightTrigger", "Dpad"};
        for (const char *axis : axes) {
            AxisRegister add = nullptr;
            Unregister remove = nullptr;
            const QByteArray prefix = QByteArray("OH_GamePad_") + axis;
            if (!resolve(add, (prefix + "_RegisterAxisInputMonitor").constData()) ||
                !resolve(remove, (prefix + "_UnregisterAxisInputMonitor").constData())) {
                qInfo() << "Harmony GameController RegistrationFailed: missing axis API" << prefix;
                return false;
            }
            const auto result = add(&HarmonyJoystickInput::_onAxis);
            if (result != GAME_CONTROLLER_SUCCESS) {
                qInfo() << "Harmony GameController RegistrationFailed:" << prefix << static_cast<int>(result);
                return false;
            }
            registered.push_back(remove);
        }
        const char *buttons[] = {"ButtonA", "ButtonB", "ButtonX", "ButtonY", "LeftShoulder", "RightShoulder",
            "ButtonHome", "ButtonMenu", "LeftThumbstick", "RightThumbstick", "Dpad_UpButton", "Dpad_DownButton",
            "Dpad_LeftButton", "Dpad_RightButton"};
        for (const char *button : buttons) {
            ButtonRegister add = nullptr;
            Unregister remove = nullptr;
            const QByteArray prefix = QByteArray("OH_GamePad_") + button;
            if (!resolve(add, (prefix + "_RegisterButtonInputMonitor").constData()) ||
                !resolve(remove, (prefix + "_UnregisterButtonInputMonitor").constData())) {
                qInfo() << "Harmony GameController RegistrationFailed: missing button API" << prefix;
                return false;
            }
            const auto result = add(&HarmonyJoystickInput::_onButton);
            if (result != GAME_CONTROLLER_SUCCESS) {
                qInfo() << "Harmony GameController RegistrationFailed:" << prefix << static_cast<int>(result);
                return false;
            }
            registered.push_back(remove);
        }
        return true;
    }
    void unregisterInputs() {
        for (auto it = registered.rbegin(); it != registered.rend(); ++it) (*it)();
        registered.clear();
    }
    bool readDevice(GameDevice_DeviceInfo *info, QString &id, QString &name) {
        GameDevice_DeviceType type = UNKNOWN;
        char *rawId = nullptr, *rawName = nullptr;
        const bool ok = GameDevice_DeviceInfo_GetDeviceType(info, &type) == GAME_CONTROLLER_SUCCESS && type == GAME_PAD &&
            GameDevice_DeviceInfo_GetDeviceId(info, &rawId) == GAME_CONTROLLER_SUCCESS &&
            GameDevice_DeviceInfo_GetName(info, &rawName) == GAME_CONTROLLER_SUCCESS;
        if (ok) {
            id = QString::fromUtf8(rawId);
            name = QString::fromUtf8(rawName);
        }
        free(rawId);
        free(rawName);
        return ok && !id.isEmpty();
    }
};
#undef GAME_API_FUNCTIONS
#endif

HarmonyJoystickInput::HarmonyJoystickInput(QObject *parent)
    : QObject(parent)
{
}

HarmonyJoystickInput::~HarmonyJoystickInput()
{
    stop();
}

HarmonyJoystickInput::Status HarmonyJoystickInput::start()
{
    stop();
#ifdef Q_OS_HARMONY
    const QString productModel = QString::fromUtf8(OH_GetProductModel());
    const bool supported = canIUse("SystemCapability.Game.GameController");
    if (productModel == QStringLiteral("emulator") || !supported) {
        qInfo() << "Harmony GameController Unsupported: model/SysCap" << productModel << supported;
        return _status = Status::Unsupported;
    }
    _nativeApi = std::make_unique<NativeApi>();
    if (!_nativeApi->load()) {
        qInfo() << "Harmony GameController Unsupported: library/API" << _nativeApi->library.errorString();
        _nativeApi.reset();
        return _status = Status::Unsupported;
    }
    {
        const std::lock_guard<std::mutex> lock(callbackMutex);
        if (callbackInput) {
            qInfo() << "Harmony GameController RegistrationFailed: another monitor is active";
            _nativeApi.reset();
            return _status = Status::RegistrationFailed;
        }
        callbackInput = this;
    }
    _status = Status::Monitoring;
    if (!_nativeApi->registerInputs()) {
        stop();
        return _status = Status::RegistrationFailed;
    }
    GameDevice_AllDeviceInfos *infos = nullptr;
    int32_t count = 0;
    auto result = _nativeApi->GameDevice_GetAllDeviceInfos(&infos);
    if (result == GAME_CONTROLLER_SUCCESS) result = _nativeApi->GameDevice_AllDeviceInfos_GetCount(infos, &count);
    if (result != GAME_CONTROLLER_SUCCESS) {
        qInfo() << "Harmony GameController RegistrationFailed: device enumeration" << static_cast<int>(result);
        if (infos) _nativeApi->GameDevice_DestroyAllDeviceInfos(&infos);
        stop();
        return _status = Status::RegistrationFailed;
    }
    for (int32_t i = 0; i < count; ++i) {
        GameDevice_DeviceInfo *info = nullptr;
        if (_nativeApi->GameDevice_AllDeviceInfos_GetDeviceInfo(infos, i, &info) == GAME_CONTROLLER_SUCCESS) {
            QString id, name;
            if (_nativeApi->readDevice(info, id, name)) _deviceAdded(id, name);
            _nativeApi->GameDevice_DestroyDeviceInfo(&info);
        }
    }
    _nativeApi->GameDevice_DestroyAllDeviceInfos(&infos);
    return _status;
#else
    return _status = Status::Unsupported;
#endif
}

void HarmonyJoystickInput::stop()
{
    ++_epoch;
    ++_lifetime;
#ifdef Q_OS_HARMONY
    {
        const std::lock_guard<std::mutex> lock(callbackMutex);
        if (callbackInput == this) callbackInput = nullptr;
    }
    if (_nativeApi) {
        _nativeApi->unregisterInputs();
        _nativeApi.reset();
    }
#endif
    _clearDevices();
    _status = Status::Stopped;
}

void HarmonyJoystickInput::setFocused(bool focused)
{
    if (_focused == focused) {
        return;
    }
    _focused = focused;
    ++_epoch;
    if (!focused) {
        SDL_LockJoysticks();
        for (Device &device : _devices) {
            _resetValues(device);
        }
        SDL_JoystickUpdate();
        SDL_UnlockJoysticks();
    }
}

int HarmonyJoystickInput::_deviceAdded(const QString &id, const QString &name)
{
    if (_status != Status::Monitoring || id.isEmpty()) {
        return -1;
    }
    if (_devices.contains(id)) {
        return _devices.value(id).instanceId;
    }
    QString deviceName = name + QStringLiteral(" [%1]").arg(id);
#if defined(QGC_UNITTEST_BUILD) || defined(QGC_HARMONY_JOYSTICK_NATIVE_TEST)
    if (_simulation) {
        deviceName.prepend(QLatin1String(testPrefix));
    }
#endif
    const QByteArray utf8Name = deviceName.toUtf8();
    SDL_VirtualJoystickDesc desc{};
    desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    desc.naxes = SDL_CONTROLLER_AXIS_MAX;
    desc.nbuttons = SDL_CONTROLLER_BUTTON_DPAD_RIGHT + 1;
    desc.axis_mask = (1u << SDL_CONTROLLER_AXIS_MAX) - 1;
    desc.button_mask = (1u << desc.nbuttons) - 1;
    desc.name = utf8Name.constData();
    SDL_LockJoysticks();
    const int index = SDL_JoystickAttachVirtualEx(&desc);
    SDL_Joystick *joystick = index >= 0 ? SDL_JoystickOpen(index) : nullptr;
    if (!joystick) {
        if (index >= 0) {
            SDL_JoystickDetachVirtual(index);
        }
        SDL_UnlockJoysticks();
        qWarning() << "Unable to create Harmony SDL gamepad:" << SDL_GetError();
        return -1;
    }
    Device device{joystick, SDL_JoystickInstanceID(joystick)};
    _resetValues(device);
    _devices.insert(id, device);
    SDL_JoystickUpdate();
    SDL_UnlockJoysticks();
    emit devicesChanged();
    return device.instanceId;
}

void HarmonyJoystickInput::_deviceRemoved(const QString &id)
{
    auto it = _devices.find(id);
    if (it == _devices.end()) {
        return;
    }
    SDL_LockJoysticks();
    _resetValues(it.value());
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_JoystickGetDeviceInstanceID(i) == it->instanceId) {
            SDL_JoystickDetachVirtual(i);
            break;
        }
    }
    SDL_JoystickClose(it->joystick);
    _devices.erase(it);
    SDL_UnlockJoysticks();
    emit devicesChanged();
}

void HarmonyJoystickInput::_resetValues(Device &device)
{
    for (int axis = 0; axis < SDL_CONTROLLER_AXIS_MAX; ++axis) {
        SDL_JoystickSetVirtualAxis(device.joystick, axis,
            axis >= SDL_CONTROLLER_AXIS_TRIGGERLEFT ? SDL_JOYSTICK_AXIS_MIN : 0);
    }
    for (int button = 0; button <= SDL_CONTROLLER_BUTTON_DPAD_RIGHT; ++button) {
        SDL_JoystickSetVirtualButton(device.joystick, button, 0);
    }
}

void HarmonyJoystickInput::_clearDevices()
{
    const QStringList ids = _devices.keys();
    for (const QString &id : ids) {
        _deviceRemoved(id);
    }
}

void HarmonyJoystickInput::_axis(const QString &id, int axis, double value)
{
    if (!_focused || !std::isfinite(value) || axis < 0 || axis >= SDL_CONTROLLER_AXIS_MAX) {
        return;
    }
    const auto it = _devices.constFind(id);
    if (it == _devices.cend()) {
        return;
    }
    value = std::clamp(value, -1.0, 1.0);
    const Sint16 raw = static_cast<Sint16>(std::lround(value * (value < 0 ? 32768.0 : 32767.0)));
    SDL_JoystickSetVirtualAxis(it->joystick, axis, raw);
}

void HarmonyJoystickInput::_button(const QString &id, int keyCode, bool down)
{
    if (!_focused) {
        return;
    }
    const auto it = _devices.constFind(id);
    const int button = buttonIndex(keyCode);
    if (it != _devices.cend() && button >= 0) {
        SDL_JoystickSetVirtualButton(it->joystick, button, down ? 1 : 0);
    }
}

#if defined(QGC_UNITTEST_BUILD) && !defined(QGC_HARMONY_JOYSTICK_NATIVE_TEST)
bool HarmonyJoystickInput::allowsVehicleControl(const QString &name, Vehicle *vehicle)
{
    if (!name.startsWith(QLatin1String(testPrefix))) {
        return true;
    }
    if (!vehicle) {
        return false;
    }
    const auto link = vehicle->vehicleLinkManager()->primaryLink().lock();
    return link && link->linkConfiguration()->type() == LinkConfiguration::TypeMock;
}
#endif

#ifdef Q_OS_HARMONY
void HarmonyJoystickInput::_onDevice(const GameDevice_DeviceEvent *event)
{
    const std::lock_guard<std::mutex> lock(callbackMutex);
    if (!callbackInput || !event) return;
    auto *input = callbackInput;
    auto &api = *input->_nativeApi;
    GameDevice_StatusChangedType type = OFFLINE;
    GameDevice_DeviceInfo *info = nullptr;
    QString id, name;
    const bool ok = api.GameDevice_DeviceEvent_GetChangedType(event, &type) == GAME_CONTROLLER_SUCCESS &&
        api.GameDevice_DeviceEvent_GetDeviceInfo(event, &info) == GAME_CONTROLLER_SUCCESS && api.readDevice(info, id, name);
    if (info) api.GameDevice_DestroyDeviceInfo(&info);
    if (!ok) return;
    const quint64 lifetime = input->_lifetime.load();
    QMetaObject::invokeMethod(input, [input, id, name, type, lifetime]() {
        if (lifetime != input->_lifetime.load()) return;
        if (type == ONLINE) input->_deviceAdded(id, name);
        else input->_deviceRemoved(id);
    }, Qt::QueuedConnection);
}

void HarmonyJoystickInput::_onAxis(const GamePad_AxisEvent *event)
{
    const std::lock_guard<std::mutex> lock(callbackMutex);
    if (!callbackInput || !event) return;
    auto *input = callbackInput;
    auto &api = *input->_nativeApi;
    char *rawId = nullptr;
    GamePad_AxisSourceType source = DPAD;
    const bool ok = api.GamePad_AxisEvent_GetDeviceId(event, &rawId) == GAME_CONTROLLER_SUCCESS &&
        api.GamePad_AxisEvent_GetAxisSourceType(event, &source) == GAME_CONTROLLER_SUCCESS;
    const QString id = QString::fromUtf8(rawId);
    free(rawId);
    if (!ok) return;
    double x = 0, y = 0;
    int axis = -1;
    GameController_ErrorCode result = GAME_CONTROLLER_PARAM_ERROR;
    switch (source) {
    case LEFT_THUMBSTICK:
        axis = 0;
        result = api.GamePad_AxisEvent_GetXAxisValue(event, &x);
        if (result == GAME_CONTROLLER_SUCCESS) result = api.GamePad_AxisEvent_GetYAxisValue(event, &y);
        break;
    case RIGHT_THUMBSTICK:
        axis = 2;
        result = api.GamePad_AxisEvent_GetZAxisValue(event, &x);
        if (result == GAME_CONTROLLER_SUCCESS) result = api.GamePad_AxisEvent_GetRZAxisValue(event, &y);
        break;
    case LEFT_TRIGGER: axis = 4; result = api.GamePad_AxisEvent_GetBrakeAxisValue(event, &x); break;
    case RIGHT_TRIGGER: axis = 5; result = api.GamePad_AxisEvent_GetGasAxisValue(event, &x); break;
    case DPAD:
        result = api.GamePad_AxisEvent_GetHatXAxisValue(event, &x);
        if (result == GAME_CONTROLLER_SUCCESS) result = api.GamePad_AxisEvent_GetHatYAxisValue(event, &y);
        break;
    }
    if (result != GAME_CONTROLLER_SUCCESS || !std::isfinite(x) || !std::isfinite(y)) return;
    const quint64 epoch = input->_epoch.load();
    QMetaObject::invokeMethod(input, [input, id, source, axis, x, y, epoch]() {
        if (epoch != input->_epoch.load()) return;
        SDL_LockJoysticks();
        if (source == DPAD) {
            input->_button(id, 2014, x < 0);
            input->_button(id, 2015, x > 0);
            input->_button(id, 2012, y < 0);
            input->_button(id, 2013, y > 0);
        } else {
            input->_axis(id, axis, x);
            if (axis < 4) input->_axis(id, axis + 1, y);
        }
        SDL_UnlockJoysticks();
    }, Qt::QueuedConnection);
}

void HarmonyJoystickInput::_onButton(const GamePad_ButtonEvent *event)
{
    const std::lock_guard<std::mutex> lock(callbackMutex);
    if (!callbackInput || !event) return;
    auto *input = callbackInput;
    auto &api = *input->_nativeApi;
    char *rawId = nullptr;
    GamePad_Button_ActionType action = UP;
    int32_t keyCode = 0;
    const bool ok = api.GamePad_ButtonEvent_GetDeviceId(event, &rawId) == GAME_CONTROLLER_SUCCESS &&
        api.GamePad_ButtonEvent_GetButtonAction(event, &action) == GAME_CONTROLLER_SUCCESS &&
        api.GamePad_ButtonEvent_GetButtonCode(event, &keyCode) == GAME_CONTROLLER_SUCCESS;
    const QString id = QString::fromUtf8(rawId);
    free(rawId);
    if (!ok) return;
    const quint64 epoch = input->_epoch.load();
    QMetaObject::invokeMethod(input, [input, id, keyCode, action, epoch]() {
        if (epoch == input->_epoch.load()) input->_button(id, keyCode, action == DOWN);
    }, Qt::QueuedConnection);
}
#endif

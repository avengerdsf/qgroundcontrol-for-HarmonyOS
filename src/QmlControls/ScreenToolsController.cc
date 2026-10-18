#include "ScreenToolsController.h"
#include "QGCApplication.h"
#include "QGCLoggingCategory.h"
#include "SettingsManager.h"
#include "AppSettings.h"

#include <QtGui/QCursor>
#include <QtGui/QFontDatabase>
#include <QtGui/QFontMetrics>
#include <QtGui/QInputDevice>

#ifdef Q_OS_HARMONY
#include <QtCore/private/qcore_ohos_p.h>
#endif

#if defined(Q_OS_IOS)
#include <sys/utsname.h>
#endif

QGC_LOGGING_CATEGORY(ScreenToolsControllerLog, "QMLControls.ScreenToolsController")

#ifdef Q_OS_HARMONY
namespace {
const QString& harmonyDeviceType()
{
    static const QString deviceType = QOhosJsThreadGateway::eval(
        [](QOhosJsState& jsState) {
            try {
                return QString::fromStdString(jsState.eval<QNapi::String>("@ohos.deviceInfo.deviceType").Utf8Value());
            } catch (const Napi::Error& exception) {
                qCWarning(ScreenToolsControllerLog) << "Failed to read HarmonyOS device type:" << exception.what();
                return QString();
            }
        },
        Q_FUNC_INFO);
    return deviceType;
}
}  // namespace
#endif

ScreenToolsController::ScreenToolsController(QObject *parent)
    : QObject(parent)
{
    // qCDebug(ScreenToolsControllerLog) << Q_FUNC_INFO << this;
}

ScreenToolsController::~ScreenToolsController()
{
    // qCDebug(ScreenToolsControllerLog) << Q_FUNC_INFO << this;
}

int ScreenToolsController::mouseX()
{
    return QCursor::pos().x();
}

int ScreenToolsController::mouseY()
{
    return QCursor::pos().y();
}

bool ScreenToolsController::isHarmonyPhone()
{
#ifdef Q_OS_HARMONY
    return harmonyDeviceType() == QStringLiteral("phone");
#else
    return false;
#endif
}

bool ScreenToolsController::isHarmonyTablet()
{
#ifdef Q_OS_HARMONY
    return harmonyDeviceType() == QStringLiteral("tablet");
#else
    return false;
#endif
}

bool ScreenToolsController::hasTouch()
{
    for (const auto &inputDevice: QInputDevice::devices()) {
        if (inputDevice->type() == QInputDevice::DeviceType::TouchScreen) {
            return true;
        }
    }
    return false;
}

QString ScreenToolsController::iOSDevice()
{
#if defined(Q_OS_IOS)
    struct utsname systemInfo;
    uname(&systemInfo);
    return QString(systemInfo.machine);
#else
    return QString();
#endif
}

QString ScreenToolsController::fixedFontFamily()
{
    return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
}

QString ScreenToolsController::normalFontFamily()
{
    //-- See App.SettinsGroup.json for index
    const int langID = SettingsManager::instance()->appSettings()->qLocaleLanguage()->rawValue().toInt();
    if (langID == QLocale::Korean) {
        return QStringLiteral("NanumGothic");
    }

    return QStringLiteral("Open Sans");
}

double ScreenToolsController::defaultFontDescent(int pointSize)
{
    return QFontMetrics(QFont(normalFontFamily(), pointSize)).descent();
}

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
bool ScreenToolsController::fakeMobile()
{
    return qgcApp()->fakeMobile();
}
#endif

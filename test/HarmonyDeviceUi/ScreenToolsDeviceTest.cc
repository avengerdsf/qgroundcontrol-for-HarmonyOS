#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#include "ScreenToolsController.h"

static std::string deviceType;
static bool readFailure = false;
static bool testFakeMobile = false;
static int deviceReads = 0;
static int gatewayCalls = 0;
static int warnings = 0;

#if !TEST_NATIVE_MOBILE
struct TestApplication {
    bool fakeMobile() const { return testFakeMobile; }
};

static TestApplication *qgcApp()
{
    static TestApplication application;
    return &application;
}
#endif

namespace Napi {
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
}

namespace QNapi {
struct String {
    std::string value;
    std::string Utf8Value() const { return value; }
};
}

struct QOhosJsState {
    template<typename T> T eval(const std::string &expression)
    {
        if (expression != "@ohos.deviceInfo.deviceType") {
            throw std::runtime_error("Unexpected device type expression");
        }
        ++deviceReads;
        if (readFailure) {
            throw Napi::Error("Device info unavailable");
        }
        return T{deviceType};
    }
};

namespace QOhosJsThreadGateway {
template<typename F> auto eval(F &&function, const char *)
{
    ++gatewayCalls;
    QOhosJsState state;
    return function(state);
}
}

struct TestWarning {
    template<typename T> TestWarning &operator<<(const T &) { return *this; }
};
#define qCWarning(...) (++warnings, TestWarning{})
#define Q_FUNC_INFO __func__

// Generated from complete production method bodies by run_tests.py.
#include "ScreenToolsDeviceMethods.inc"

static void require(bool condition, const char *message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

int main(int argc, char **argv)
{
    try {
        require(argc == 6, "Expected device type, failure, phone, tablet, mobile");
        deviceType = argv[1];
        readFailure = std::atoi(argv[2]);
        const bool wantPhone = std::atoi(argv[3]);
        const bool wantTablet = std::atoi(argv[4]);
        const bool wantMobile = std::atoi(argv[5]);
        require(ScreenToolsController::isMobile() == wantMobile, "Wrong mobile UI");
        require(ScreenToolsController::isHarmonyTablet() == wantTablet, "Wrong tablet policy");
        require(ScreenToolsController::isHarmonyPhone() == wantPhone, "Wrong phone policy");
        require(ScreenToolsController::isAndroid() == bool(TEST_ANDROID), "Android changed");
        require(ScreenToolsController::isiOS() == bool(TEST_IOS), "iOS changed");
        require(!ScreenToolsController::fakeMobile(), "Unexpected simulated mobile UI");

        // A later read, including an error, must not change the chosen device UI.
        deviceType = wantTablet ? "phone" : "tablet";
        readFailure = !readFailure;
        require(ScreenToolsController::isHarmonyTablet() == wantTablet, "Device type was not cached");
        require(ScreenToolsController::isHarmonyPhone() == wantPhone, "Phone type was not cached");
        require(ScreenToolsController::isMobile() == wantMobile, "Cached mobile UI changed");
#ifdef Q_OS_HARMONY
        require(deviceReads == 1 && gatewayCalls == 1, "Device info queried more than once");
        require(warnings == std::atoi(argv[2]), "Unexpected device read warnings");
#else
        require(deviceReads == 0 && gatewayCalls == 0, "Other platform read HarmonyOS info");
        require(warnings == 0, "Other platform logged a HarmonyOS warning");
#endif

        testFakeMobile = true;
        require(ScreenToolsController::isMobile(), "Desktop mobile simulation changed");
        require(ScreenToolsController::fakeMobile() == !bool(TEST_NATIVE_MOBILE),
                "Native mobile simulation changed");
        testFakeMobile = false;
        require(ScreenToolsController::isMobile() == wantMobile, "Mobile simulation stayed enabled");
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

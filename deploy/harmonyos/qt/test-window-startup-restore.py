#!/usr/bin/env python3
"""Compile Qt's real showImmediate and optional status query against controlled platform boundaries."""

import argparse
import json
import os
import subprocess
import tempfile
from pathlib import Path


def block(source, marker):
    start = source.index(marker)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main():
    root = Path(__file__).resolve().parents[3]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=root / "tmp/qt-src/qt-everywhere-src-6.12.0")
    parser.add_argument("--output", type=Path)
    parser.add_argument(
        "--cxx", type=Path, default=root / "tmp/toolchains/Qt/Tools/mingw1310_64/bin/g++.exe"
    )
    args = parser.parse_args()
    render = args.source / "qtbase/src/plugins/platforms/ohos/render"
    show = block(
        (render / "qohosview.cpp").read_text(encoding="utf-8"), "void QOhosView::showImmediate()"
    )
    proxy_source = (render / "qohoswindowproxy.cpp").read_text(encoding="utf-8")
    query_marker = "std::optional<QOhosWindowProxy::WindowStatusType> QOhosWindowProxy::tryGetWindowStatus() const"
    query = block(proxy_source, query_marker) if query_marker in proxy_source else ""
    view_source = (render / "qohosview.cpp").read_text(encoding="utf-8")
    persistence = block(
        view_source, "WindowGeometryPersistenceState syncWindowGeometryPersistenceState("
    )
    submit = block(view_source, "auto submitSystemPropertyUpdate =") + ";"
    submit_or_reset = block(view_source, "auto submitOrResetSystemPropertyUpdate =") + ";"
    persistence_start = view_source.index("auto windowGeometryPersistenceState =")
    persistence_state = view_source[
        persistence_start : view_source.index(";", persistence_start) + 1
    ]
    harness = r"""
#include <algorithm>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#define Q_FUNC_INFO __func__
std::vector<std::string> calls;
void qOhosPrintfDebug(const char *) {}
void qCWarning(int, const char *, const char *, const char *) { calls.push_back("status-error"); }
constexpr int QtForOhos = 0;
namespace Napi { struct Error : std::runtime_error { using std::runtime_error::runtime_error; }; }
namespace QNapi { struct Number { int value; }; }
enum class NativeStatus { UNDEFINED, FULL_SCREEN, MAXIMIZE, MINIMIZE, FLOATING, SPLIT_SCREEN };
enum class WindowGeometryPersistenceState { Disabled, Enabled };
enum class ViewGeometryPersistencePolicy { Disabled, Enabled, FollowSystemSetting, Ignore };
struct QOhosPlatformIntegration {
    enum class WindowGeometryPersistencePolicy { Disabled, Enabled, FollowSystemSetting };
    static inline auto persistencePolicy = WindowGeometryPersistencePolicy::Disabled;
    static auto getMainWindowGeometryPersistencePolicy() { return persistencePolicy; }
};
namespace QtOhos {
struct JsState {
    template<class T> std::optional<T> tryMapOhosEnumFromJs(QNapi::Number number) {
        if (number.value < 0 || number.value > 5) return {};
        return static_cast<T>(number.value);
    }
};
template<class F> auto evalInJsThread(F f, const char *) { JsState state; return f(state); }
}
struct JsRef {
    int status = int(NativeStatus::FULL_SCREEN);
    bool error = false;
    template<class T> T eval(const char *method) {
        if (std::string(method) != "getWindowStatus()") throw std::runtime_error("wrong API");
        calls.push_back("query-status");
        if (error) throw Napi::Error("801");
        return { status };
    }
};
struct JsScopeData {
    std::shared_ptr<JsRef> jsWindowRef = std::make_shared<JsRef>();
    bool closing = false;
    bool isWindowClosing() const { return closing; }
};
struct Flags {
    int value = 0;
    template<class T> bool testFlag(T flag) const { return (value & int(flag)) != 0; }
};
namespace Qt {
enum WindowState { WindowNoState = 0, WindowMinimized = 1, WindowMaximized = 2, WindowFullScreen = 4 };
constexpr int ExpandedClientAreaHint = 8;
}
struct Rect { int x = 80, y = 158, width = 1715, height = 1286; bool operator==(const Rect &) const = default; };
struct FakeWindow {
    Flags states, windowFlags;
    Rect requested;
    Flags windowStates() const { return states; }
    Flags flags() const { return windowFlags; }
    void close() { calls.push_back("close"); }
};
struct WindowRef {
    FakeWindow *ptr;
    FakeWindow *operator->() const { return ptr; }
    operator FakeWindow *() const { return ptr; }
    bool isNull() const { return ptr == nullptr; }
};
struct QOhosWindowProxy {
    using WindowStatusType = NativeStatus;
    std::shared_ptr<JsScopeData> m_jsScopeData = std::make_shared<JsScopeData>();
    FakeWindow *owner = nullptr;
    Rect nativeRect { 900, 900, 700, 600 };
    bool autoSave = false, setSupported = true, querySupported = true;
    void setWindowRectAutoSave(bool enabled) {
        calls.push_back(enabled ? "autosave-on" : "autosave-off");
        if (setSupported) autoSave = enabled;
    }
    bool isWindowRectAutoSave() const {
        calls.push_back("query-autosave");
        return querySupported && autoSave;
    }
    std::optional<WindowStatusType> tryGetWindowStatus() const;
    void recover() {
        calls.push_back("recover"); m_jsScopeData->jsWindowRef->status = int(NativeStatus::FLOATING);
        if (owner) owner->states.value = Qt::WindowNoState; // Native completion may update Qt states.
    }
    int nodeXComponent() const { return 1; }
    template<class F> void setSubWindowCloseHandler(F, bool) { calls.push_back("sub-close-handler"); }
    void setFollowParentMultiScreenPolicy(bool) { calls.push_back("follow-parent"); }
};
QUERY
PERSISTENCE
struct QOhosDeviceInfo {
    static inline bool phone = false, tablet = true;
    static bool isPhone() { return phone; }
    static bool isTablet() { return tablet; }
    static bool is2in1() { return !phone && !tablet; }
};
struct QOhosSettings {
    bool pcMode = true;
    static QOhosSettings &instance() { static QOhosSettings value; return value; }
    bool isWindowPcModeEnabled() const { return pcMode; }
};
struct QOhosPlatformWindow { static FakeWindow *fromQWindow(FakeWindow *window) { return window; } };
struct QOhosCloseEventContext {
    enum class CloseRootCause { SubWindowClose };
    template<class F> static void runWithCloseRootCauseSet(CloseRootCause, F f) { f(); }
};
struct Node {
    void setParent(int) { calls.push_back("parent"); }
    void fillToParent() { calls.push_back("fill"); }
    void setVisibility(bool visible) { if (visible) calls.push_back("visible"); }
};
struct Surface { void clearNativeWindowSurface() { calls.push_back("surface-clear"); } };
struct QOhosView {
    enum class ViewType { MainWindow, SubWindow, EmbeddedWindow, FloatWindow };
    struct ViewTypeInfo { ViewType viewType; FakeWindow *optLogicalParent = nullptr; };
    FakeWindow window;
    WindowRef m_ownerWindow { &window };
    std::shared_ptr<QOhosWindowProxy> m_ohosWindowProxy;
    std::shared_ptr<QOhosWindowProxy> createdProxy = std::make_shared<QOhosWindowProxy>();
    std::optional<ViewType> m_viewType;
    ViewType targetType = ViewType::MainWindow;
    Node node;
    Node *m_nativeNode = &node;
    Surface surface;
    ViewGeometryPersistencePolicy m_geometryPersistencePolicy = ViewGeometryPersistencePolicy::Enabled;
    std::shared_ptr<QOhosWindowProxy> tryCreateWindowProxyIfNeeded(ViewType type, FakeWindow *) {
        calls.push_back("create");
        if (type == ViewType::EmbeddedWindow) return nullptr;
        createdProxy->owner = &window;
        return createdProxy;
    }
    void setOrResetWindowProxy(std::shared_ptr<QOhosWindowProxy> proxy, FakeWindow *,
                              std::optional<bool> geometryPersistenceEnabled = {}) {
        m_ohosWindowProxy = proxy; syncWindowStateImmediate(geometryPersistenceEnabled);
    }
    template<class T> struct Property { std::optional<T> optPendingUpdateRequest; };
    template<class T> bool isSystemUpdatePropertySupported(const Property<T> &) const { return true; }
    void syncWindowStateImmediate(std::optional<bool> geometryPersistenceEnabled = {}) {
        calls.push_back("geometry");
        if (!m_ohosWindowProxy) return;
        (void)geometryPersistenceEnabled;
        PERSISTENCE_STATE
        SUBMIT
        SUBMIT_OR_RESET
        Property<Rect> size, position;
        submitOrResetSystemPropertyUpdate(size, window.requested, windowGeometryPersistenceState);
        submitOrResetSystemPropertyUpdate(position, window.requested, windowGeometryPersistenceState);
        if (size.optPendingUpdateRequest) {
            calls.push_back("size");
            m_ohosWindowProxy->nativeRect.width = window.requested.width;
            m_ohosWindowProxy->nativeRect.height = window.requested.height;
        }
        if (position.optPendingUpdateRequest) {
            calls.push_back("position");
            m_ohosWindowProxy->nativeRect.x = window.requested.x;
            m_ohosWindowProxy->nativeRect.y = window.requested.y;
        }
    }
    ViewType viewType() const { return m_viewType.value_or(targetType); }
    void setFullScreen() { calls.push_back("fullscreen"); window.states.value = Qt::WindowFullScreen; }
    void maximize() { calls.push_back("maximize"); window.states.value = Qt::WindowMaximized; }
    void recover() { m_ohosWindowProxy->recover(); }
    void applyPhoneWindowChrome() { calls.push_back("phone-chrome"); }
    void restoreMainWindow() { calls.push_back("restore-main"); }
    void showWindow() { calls.push_back("show"); }
    Surface *surfaceOrNull() { return &surface; }
    void showImmediate();
};
QOhosView::ViewTypeInfo determineViewTypeAndLogicalParent(FakeWindow *) {
    // Each test sets the requested role through this controlled boundary.
    extern QOhosView::ViewType requestedRole;
    return { requestedRole };
}
QOhosView::ViewType requestedRole = QOhosView::ViewType::MainWindow;
SHOW
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
int count(const std::string &name) { return int(std::count(calls.begin(), calls.end(), name)); }
int index(const std::string &name) {
    auto found = std::find(calls.begin(), calls.end(), name);
    return found == calls.end() ? -1 : int(found - calls.begin());
}
void order(const char *left, const char *right) { require(index(left) >= 0 && index(left) < index(right), "wrong native operation order"); }
int main() {
    int failures = 0, passed = 0;
    auto test = [&](const char *name, auto run) {
        calls.clear(); requestedRole = QOhosView::ViewType::MainWindow;
        QOhosDeviceInfo::phone = false; QOhosDeviceInfo::tablet = true;
        QOhosSettings::instance().pcMode = true;
        QOhosPlatformIntegration::persistencePolicy = QOhosPlatformIntegration::WindowGeometryPersistencePolicy::Disabled;
        try { run(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception &error) { ++failures; std::cout << "FAIL " << name << ": " << error.what() << '\n'; }
    };
    test("Tablet PC windowed cold start recovers before restoring saved geometry and showing", [] {
        QOhosView view; view.showImmediate();
        require(count("recover") == 1, "initial native full screen was not recovered");
        order("recover", "geometry"); order("geometry", "show"); order("show", "visible");
        require(view.createdProxy->nativeRect == view.window.requested, "saved normal rectangle was replaced by native old rectangle");
        require(count("fullscreen") == 0 && count("maximize") == 0, "windowed target was overridden");
    });
    test("Tablet PC fullscreen cold start applies final state after recover and geometry", [] {
        QOhosView view; view.window.states.value = Qt::WindowFullScreen; view.showImmediate();
        order("recover", "geometry"); order("geometry", "fullscreen"); order("fullscreen", "show");
        require(view.window.states.testFlag(Qt::WindowFullScreen), "native recover event overwrote desired fullscreen");
    });
    test("Tablet PC maximized cold start preserves requested state across recover events", [] {
        QOhosView view; view.window.states.value = Qt::WindowMaximized; view.showImmediate();
        order("recover", "geometry"); order("geometry", "maximize"); order("maximize", "show");
    });
    test("already floating native window is not recovered again", [] {
        QOhosView view; view.createdProxy->m_jsScopeData->jsWindowRef->status = int(NativeStatus::FLOATING); view.showImmediate();
        require(count("recover") == 0 && count("geometry") == 1 && count("show") == 1, "floating start repeated recovery");
    });
    test("already floating cold fullscreen target explicitly enters fullscreen", [] {
        QOhosView view; view.createdProxy->m_jsScopeData->jsWindowRef->status = int(NativeStatus::FLOATING);
        view.window.states.value = Qt::WindowFullScreen; view.showImmediate();
        require(count("recover") == 0 && count("fullscreen") == 1, "fullscreen target relied on stale native creation default");
    });
    test("native maximized and split starts recover before saved geometry", [] {
        for (auto status : { NativeStatus::MAXIMIZE, NativeStatus::SPLIT_SCREEN }) {
            calls.clear(); QOhosView view; view.createdProxy->m_jsScopeData->jsWindowRef->status = int(status); view.showImmediate();
            require(count("recover") == 1, "recoverable native state was ignored"); order("recover", "geometry");
        }
    });
    test("unknown native status never triggers recovery", [] {
        QOhosView view; view.createdProxy->m_jsScopeData->jsWindowRef->status = 999; view.showImmediate();
        require(count("recover") == 0 && count("show") == 1, "unknown status was recovered or blocked showing");
    });
    test("unsupported native status query is nonfatal and does not recover", [] {
        QOhosView view; view.createdProxy->m_jsScopeData->jsWindowRef->error = true; view.showImmediate();
        require(count("recover") == 0 && count("show") == 1 && count("status-error") == 1, "unsupported query did not fall back safely");
    });
    test("ordinary Tablet keeps its native fullscreen path", [] {
        QOhosSettings::instance().pcMode = false; QOhosView view; view.window.states.value = Qt::WindowFullScreen; view.showImmediate();
        require(count("query-status") == 0 && count("recover") == 0 && count("fullscreen") == 0, "ordinary Pad path changed");
    });
    test("phone fullscreen chrome remains explicit", [] {
        QOhosDeviceInfo::tablet = false; QOhosDeviceInfo::phone = true;
        QOhosView view; view.window.states.value = Qt::WindowFullScreen; view.showImmediate();
        require(count("query-status") == 0 && count("recover") == 0 && count("fullscreen") == 1, "phone fullscreen path changed");
    });
    test("phone expanded client chrome is retained", [] {
        QOhosDeviceInfo::tablet = false; QOhosDeviceInfo::phone = true;
        QOhosView view; view.window.windowFlags.value = Qt::ExpandedClientAreaHint; view.showImmediate();
        require(count("phone-chrome") == 1 && count("recover") == 0, "phone expanded chrome changed");
    });
    test("2in1 unsupported autosave restores application geometry before final fullscreen", [] {
        QOhosDeviceInfo::tablet = false;
        QOhosPlatformIntegration::persistencePolicy = QOhosPlatformIntegration::WindowGeometryPersistencePolicy::Enabled;
        QOhosView view; view.createdProxy->setSupported = false;
        view.window.states.value = Qt::WindowFullScreen; view.showImmediate();
        require(count("recover") == 1 && count("fullscreen") == 1 && count("size") == 1 && count("position") == 1,
                "2in1 autosave failure did not use application geometry fallback");
        order("recover", "geometry"); order("geometry", "fullscreen"); order("fullscreen", "show");
    });
    test("subwindow keeps close handler and parent multi-screen policy", [] {
        requestedRole = QOhosView::ViewType::SubWindow; QOhosView view; view.targetType = requestedRole; view.showImmediate();
        require(count("query-status") == 0 && count("recover") == 0, "child window was normalized");
        require(count("sub-close-handler") == 1 && count("follow-parent") == 1, "child window behavior changed");
    });
    test("float and embedded roles never receive main-window recovery", [] {
        for (auto role : { QOhosView::ViewType::FloatWindow, QOhosView::ViewType::EmbeddedWindow }) {
            calls.clear(); requestedRole = role; QOhosView view; view.targetType = role; view.showImmediate();
            require(count("query-status") == 0 && count("recover") == 0, "non-main role was normalized");
        }
    });
    test("existing proxy re-show never repeats initial recovery", [] {
        QOhosView view; view.m_ohosWindowProxy = view.createdProxy; view.m_viewType = QOhosView::ViewType::MainWindow;
        view.window.states.value = Qt::WindowFullScreen; view.showImmediate();
        require(count("query-status") == 0 && count("recover") == 0 && count("fullscreen") == 1, "re-show behavior changed");
    });
    test("initial minimized and undefined native states are not recovered", [] {
        for (auto status : { NativeStatus::MINIMIZE, NativeStatus::UNDEFINED }) {
            calls.clear(); QOhosView view; view.createdProxy->m_jsScopeData->jsWindowRef->status = int(status); view.showImmediate();
            require(count("recover") == 0 && count("show") == 1, "inactive state triggered unsupported recovery");
        }
    });
    test("Enabled hint initializes a previously disabled Stage before the first recovery decision", [] {
        QOhosPlatformIntegration::persistencePolicy = QOhosPlatformIntegration::WindowGeometryPersistencePolicy::Enabled;
        QOhosView view; view.createdProxy->m_jsScopeData->jsWindowRef->status = int(NativeStatus::FLOATING);
        const auto originalRect = view.createdProxy->nativeRect; view.showImmediate();
        require(count("autosave-on") == 1 && count("query-autosave") == 1, "initial persistence was not queried once and reused");
        order("autosave-on", "query-autosave"); order("query-autosave", "query-status");
        require(count("recover") == 0 && count("size") == 0 && count("position") == 0,
                "system geometry authority was overwritten");
        require(view.createdProxy->nativeRect == originalRect, "native cached rectangle changed");
    });
    test("cached fullscreen target already matching native mode performs no window transition", [] {
        QOhosPlatformIntegration::persistencePolicy = QOhosPlatformIntegration::WindowGeometryPersistencePolicy::Enabled;
        QOhosView view; view.window.states.value = Qt::WindowFullScreen; view.showImmediate();
        require(count("recover") == 0 && count("fullscreen") == 0 && count("maximize") == 0,
                "already fullscreen startup took a visible round trip");
        require(count("size") == 0 && count("position") == 0 && count("show") == 1, "cached fullscreen geometry changed");
    });
    test("cached maximized target already matching native mode is shown directly", [] {
        QOhosPlatformIntegration::persistencePolicy = QOhosPlatformIntegration::WindowGeometryPersistencePolicy::Enabled;
        QOhosView view; view.window.states.value = Qt::WindowMaximized;
        view.createdProxy->m_jsScopeData->jsWindowRef->status = int(NativeStatus::MAXIMIZE); view.showImmediate();
        require(count("recover") == 0 && count("maximize") == 0 && count("show") == 1, "matching maximized mode changed");
    });
    test("cached floating to requested fullscreen changes mode exactly once without recovery or geometry writes", [] {
        QOhosPlatformIntegration::persistencePolicy = QOhosPlatformIntegration::WindowGeometryPersistencePolicy::Enabled;
        QOhosView view; view.window.states.value = Qt::WindowFullScreen;
        view.createdProxy->m_jsScopeData->jsWindowRef->status = int(NativeStatus::FLOATING); view.showImmediate();
        require(count("recover") == 0 && count("fullscreen") == 1 && count("size") == 0 && count("position") == 0,
                "fullscreen mode selection changed cached geometry"); order("fullscreen", "show");
    });
    test("cached fullscreen to requested maximized changes mode once without a floating detour", [] {
        QOhosPlatformIntegration::persistencePolicy = QOhosPlatformIntegration::WindowGeometryPersistencePolicy::Enabled;
        QOhosView view; view.window.states.value = Qt::WindowMaximized; view.showImmediate();
        require(count("recover") == 0 && count("maximize") == 1 && count("fullscreen") == 0,
                "maximized mode selection took a floating detour"); order("maximize", "show");
    });
    test("cached fullscreen to requested windowed recovers only once and preserves system geometry", [] {
        QOhosPlatformIntegration::persistencePolicy = QOhosPlatformIntegration::WindowGeometryPersistencePolicy::Enabled;
        QOhosView view; view.showImmediate();
        require(count("recover") == 1 && count("size") == 0 && count("position") == 0,
                "windowed mode wrote application geometry or repeated recovery"); order("recover", "show");
    });
    test("unsupported autosave enable safely falls back to saved application geometry", [] {
        QOhosPlatformIntegration::persistencePolicy = QOhosPlatformIntegration::WindowGeometryPersistencePolicy::Enabled;
        QOhosView view; view.createdProxy->setSupported = false; view.showImmediate();
        require(count("recover") == 1 && count("size") == 1 && count("position") == 1 && count("show") == 1,
                "unsupported autosave did not preserve application fallback"); order("recover", "geometry");
    });
    test("failed autosave query uses the same safe geometry fallback", [] {
        QOhosPlatformIntegration::persistencePolicy = QOhosPlatformIntegration::WindowGeometryPersistencePolicy::Enabled;
        QOhosView view; view.createdProxy->querySupported = false; view.showImmediate();
        require(count("recover") == 1 && count("size") == 1 && count("position") == 1,
                "unavailable autosave query disabled the application fallback");
    });
    test("following an already enabled system setting does not overwrite native geometry", [] {
        QOhosPlatformIntegration::persistencePolicy = QOhosPlatformIntegration::WindowGeometryPersistencePolicy::FollowSystemSetting;
        QOhosView view; view.createdProxy->autoSave = true;
        view.createdProxy->m_jsScopeData->jsWindowRef->status = int(NativeStatus::FLOATING); view.showImmediate();
        require(count("autosave-on") == 0 && count("autosave-off") == 0 && count("query-autosave") == 1,
                "system setting was changed or queried repeatedly");
        require(count("size") == 0 && count("position") == 0 && count("recover") == 0, "cached system geometry changed");
    });
    test("cached geometry with an unavailable mode query explicitly chooses fullscreen without a detour", [] {
        QOhosPlatformIntegration::persistencePolicy = QOhosPlatformIntegration::WindowGeometryPersistencePolicy::Enabled;
        QOhosView view; view.window.states.value = Qt::WindowFullScreen;
        view.createdProxy->m_jsScopeData->jsWindowRef->error = true; view.showImmediate();
        require(count("recover") == 0 && count("fullscreen") == 1 && count("size") == 0 && count("position") == 0,
                "unavailable mode query changed cached geometry or skipped final mode");
        require(count("status-error") == 1 && count("show") == 1, "mode-query error prevented showing");
    });
    test("2in1 cached floating startup is shown without rewriting native geometry", [] {
        QOhosDeviceInfo::tablet = false; QOhosSettings::instance().pcMode = false;
        QOhosPlatformIntegration::persistencePolicy = QOhosPlatformIntegration::WindowGeometryPersistencePolicy::Enabled;
        QOhosView view; view.createdProxy->m_jsScopeData->jsWindowRef->status = int(NativeStatus::FLOATING);
        view.showImmediate();
        require(count("autosave-on") == 1 && count("query-autosave") == 1 && count("query-status") == 1,
                "2in1 startup did not use the shared persistence decision");
        require(count("recover") == 0 && count("size") == 0 && count("position") == 0 && count("show") == 1,
                "2in1 cached floating geometry changed");
    });
    test("2in1 matching cached fullscreen startup does not assume or reapply fullscreen", [] {
        QOhosDeviceInfo::tablet = false;
        QOhosPlatformIntegration::persistencePolicy = QOhosPlatformIntegration::WindowGeometryPersistencePolicy::Enabled;
        QOhosView view; view.window.states.value = Qt::WindowFullScreen; view.showImmediate();
        require(count("query-status") == 1 && count("recover") == 0 && count("fullscreen") == 0,
                "2in1 matching fullscreen was not checked or took a detour");
        require(count("size") == 0 && count("position") == 0 && count("show") == 1, "2in1 cached fullscreen geometry changed");
    });
    test("2in1 cached floating to fullscreen selects the requested mode exactly once", [] {
        QOhosDeviceInfo::tablet = false;
        QOhosPlatformIntegration::persistencePolicy = QOhosPlatformIntegration::WindowGeometryPersistencePolicy::Enabled;
        QOhosView view; view.window.states.value = Qt::WindowFullScreen;
        view.createdProxy->m_jsScopeData->jsWindowRef->status = int(NativeStatus::FLOATING); view.showImmediate();
        require(count("fullscreen") == 1 && count("recover") == 0 && count("size") == 0 && count("position") == 0,
                "2in1 fullscreen selection relied on the native creation default"); order("fullscreen", "show");
    });
    test("2in1 cached fullscreen to floating changes only the window mode", [] {
        QOhosDeviceInfo::tablet = false;
        QOhosPlatformIntegration::persistencePolicy = QOhosPlatformIntegration::WindowGeometryPersistencePolicy::Enabled;
        QOhosView view; view.showImmediate();
        require(count("recover") == 1 && count("size") == 0 && count("position") == 0 && count("show") == 1,
                "2in1 floating mode did not preserve native cached geometry");
    });
#ifdef HAS_QUERY
    test("status query skips a closing window", [] {
        QOhosWindowProxy proxy; proxy.m_jsScopeData->closing = true;
        require(!proxy.tryGetWindowStatus().has_value() && count("query-status") == 0, "closing window accessed native API");
    });
    test("status query maps all supported native states and rejects unknown values", [] {
        QOhosWindowProxy proxy;
        for (int value = 0; value <= 5; ++value) {
            proxy.m_jsScopeData->jsWindowRef->status = value;
            require(proxy.tryGetWindowStatus() == NativeStatus(value), "native status mapping changed");
        }
        proxy.m_jsScopeData->jsWindowRef->status = 999;
        require(!proxy.tryGetWindowStatus().has_value(), "unknown enum was forced into a known state");
    });
#endif
    std::cout << "TOTAL passed=" << passed << " failed=" << failures << '\n';
    return failures ? 1 : 0;
}
"""
    for key, value in [
        ("SHOW", show),
        ("QUERY", query),
        ("PERSISTENCE", persistence),
        ("PERSISTENCE_STATE", persistence_state),
        ("SUBMIT_OR_RESET", submit_or_reset),
        ("SUBMIT", submit),
    ]:
        harness = (
            harness.replace("\n" + key + "\n", "\n" + value + "\n")
            if key in ("SHOW", "QUERY", "PERSISTENCE")
            else harness.replace(key, value)
        )
    if query:
        harness = "#define HAS_QUERY\n" + harness
    else:
        harness = harness.replace(
            "std::optional<WindowStatusType> tryGetWindowStatus() const;",
            "std::optional<WindowStatusType> tryGetWindowStatus() const { return {}; }",
        )
    temp_root = root / "tmp/qt-window-startup-restore-test"
    temp_root.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TEMP=str(temp_root), TMP=str(temp_root), TMPDIR=str(temp_root))
    env["PATH"] = str(args.cxx.parent) + os.pathsep + env.get("PATH", "")
    try:
        with tempfile.TemporaryDirectory(dir=temp_root) as work:
            cpp, exe = Path(work) / "window-startup.cpp", Path(work) / "window-startup.exe"
            cpp.write_text(harness, encoding="utf-8")
            subprocess.run(
                [
                    str(args.cxx),
                    "-std=c++20",
                    "-Wall",
                    "-Wextra",
                    "-static-libgcc",
                    "-static-libstdc++",
                    str(cpp),
                    "-o",
                    str(exe),
                ],
                check=True,
                env=env,
            )
            result = subprocess.run(
                [str(exe)], text=True, encoding="utf-8", capture_output=True, env=env
            )
            print(result.stdout, end="")
            if result.stderr:
                print(result.stderr, end="")
            entries = [
                {"name": line.split(" ", 1)[1], "status": line.split(" ", 1)[0].lower()}
                for line in result.stdout.splitlines()
                if line.startswith(("PASS ", "FAIL "))
            ]
            report = {
                "scope": "Extracted production Qt showImmediate, status query, persistence policy and geometry submission with controlled OS boundaries",
                "passed": sum(entry["status"] == "pass" for entry in entries),
                "failed": sum(entry["status"] == "fail" for entry in entries),
                "skipped": 0,
                "nativeQueryImplemented": bool(query),
                "results": entries,
            }
            if args.output:
                args.output.write_text(
                    json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
                )
            raise SystemExit(result.returncode)
    finally:
        temp_root.rmdir()


if __name__ == "__main__":
    main()

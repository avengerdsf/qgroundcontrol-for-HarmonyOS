#!/usr/bin/env python3
"""Compile and exercise Qt's window-mode submission/setter/flush code on the host."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def block(source: str, marker: str) -> str:
    start = source.index(marker)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def template_function(source: str, marker: str) -> str:
    start = source.rfind("template <", 0, source.index(marker))
    return source[start:source.index(marker)] + block(source, marker)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parents[3]
    parser.add_argument("--source", type=Path,
                        default=root / "tmp/qt-src/qt-everywhere-src-6.12.0")
    parser.add_argument("--cxx", type=Path,
                        default=root / "tmp/toolchains/Qt/Tools/mingw1310_64/bin/g++.exe")
    args = parser.parse_args()
    render = args.source / "qtbase/src/plugins/platforms/ohos/render"
    header = (render / "qohosview.h").read_text(encoding="utf-8")
    source = (render / "qohosview.cpp").read_text(encoding="utf-8")
    support_marker = "bool QOhosView::isSystemUpdatePropertySupported"
    support_decl = ""
    support_impl = ""
    if support_marker in header:
        support_decl = """
        template <typename T, QOhosRuntimeDeviceTypeAndMode SupportedModes>
        bool isSystemUpdatePropertySupported(
            const SystemUpdateDataProperty<T, SupportedModes> &) const;
        """
        support_impl = template_function(header, support_marker)
    setter = template_function(header, "void QOhosView::setSystemUpdateProperty")
    flush = block(source, "void QOhosView::flushSystemPropertyUpdatesImmediate()")
    submit = block(source, "auto submitSystemPropertyUpdate =") + ";"
    harness = r"""
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

enum class QOhosRuntimeDeviceTypeAndMode {
    _2in1 = 1, HandheldDeviceFullScreen = 2, HandheldDeviceWindowPcMode = 4
};
constexpr auto operator|(QOhosRuntimeDeviceTypeAndMode a,
                         QOhosRuntimeDeviceTypeAndMode b) {
    return QOhosRuntimeDeviceTypeAndMode(int(a) | int(b));
}
template <typename T> struct QFlags {
    T value;
    explicit QFlags(T v) : value(v) {}
    bool testFlag(T flag) const { return (int(value) & int(flag)) == int(flag); }
};
struct QOhosSettings {
    bool pcMode = false;
    static QOhosSettings &instance() { static QOhosSettings settings; return settings; }
    bool isWindowPcModeEnabled() const { return pcMode; }
};
namespace QtOhos {
template <typename Tuple, typename F> void tupleForEach(const Tuple &tuple, F &&f) {
    std::apply([&](const auto &...item) { (f(item), ...); }, tuple);
}
}
enum class ViewType { MainWindow, SubWindow };
struct QOhosView {
    static constexpr auto allModes = QOhosRuntimeDeviceTypeAndMode::_2in1
        | QOhosRuntimeDeviceTypeAndMode::HandheldDeviceFullScreen
        | QOhosRuntimeDeviceTypeAndMode::HandheldDeviceWindowPcMode;
    template <typename T, QOhosRuntimeDeviceTypeAndMode modes = allModes>
    struct SystemUpdateDataProperty { std::optional<T> optPendingUpdateRequest; };
    template <typename T> using WindowModeOnlySystemUpdateDataProperty =
        SystemUpdateDataProperty<T, QOhosRuntimeDeviceTypeAndMode::_2in1
            | QOhosRuntimeDeviceTypeAndMode::HandheldDeviceWindowPcMode>;
    struct SystemUpdateData {
        WindowModeOnlySystemUpdateDataProperty<std::string> title;
        WindowModeOnlySystemUpdateDataProperty<int> windowMinMaxCloseButtonsState;
        WindowModeOnlySystemUpdateDataProperty<bool> frameless;
        SystemUpdateDataProperty<int> backgroundColor;
    };
    bool m_requireHandheldDeviceSupport = true;
    ViewType type = ViewType::MainWindow;
    SystemUpdateData m_updateData;
    bool m_updatePending = false;
    int titleCalls = 0, buttonCalls = 0, framelessCalls = 0, backgroundCalls = 0;
    std::string appliedTitle;
    ViewType viewType() const { return type; }
    void scheduleSystemUpdateIfNeeded() { m_updatePending = true; }
    void updateWindowTitle(const std::string &v) { ++titleCalls; appliedTitle = v; }
    void updateWindowMinMaxCloseButtonsState(const int &) { ++buttonCalls; }
    void updateWindowFrameless(bool) { ++framelessCalls; }
    void updateWindowBackgroundColor(const int &) { ++backgroundCalls; }
    static constexpr auto makeSystemUpdateDataPropertyUpdateFuncPairsTuple() {
        return std::make_tuple(
            std::make_pair(&SystemUpdateData::title, &QOhosView::updateWindowTitle),
            std::make_pair(&SystemUpdateData::windowMinMaxCloseButtonsState,
                           &QOhosView::updateWindowMinMaxCloseButtonsState),
            std::make_pair(&SystemUpdateData::frameless, &QOhosView::updateWindowFrameless),
            std::make_pair(&SystemUpdateData::backgroundColor, &QOhosView::updateWindowBackgroundColor));
    }
    SUPPORT_DECL
    template <typename T, QOhosRuntimeDeviceTypeAndMode SupportedModes>
    void setSystemUpdateProperty(
        SystemUpdateDataProperty<T, SupportedModes> SystemUpdateData::*, const T &);
    void flushSystemPropertyUpdatesImmediate();
    void initialSync() {
        SUBMIT
        submitSystemPropertyUpdate(m_updateData.title, std::string("QGC"));
        submitSystemPropertyUpdate(m_updateData.windowMinMaxCloseButtonsState, 7);
        submitSystemPropertyUpdate(m_updateData.frameless, false);
        submitSystemPropertyUpdate(m_updateData.backgroundColor, 42);
    }
};
SUPPORT_IMPL
SETTER
FLUSH

void require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
void requireDesktopCalls(const QOhosView &view, int count, const char *message) {
    require(view.titleCalls == count && view.buttonCalls == count
        && view.framelessCalls == count, message);
}
int main() {
    int failures = 0;
    auto test = [&](const char *name, auto run) {
        QOhosSettings::instance().pcMode = false;
        try { run(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception &error) {
            ++failures;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    };
    test("ordinary Pad initial sync skips desktop calls", [] {
        QOhosView view;
        view.initialSync();
        require(!view.m_updateData.title.optPendingUpdateRequest
            && !view.m_updateData.windowMinMaxCloseButtonsState.optPendingUpdateRequest
            && !view.m_updateData.frameless.optPendingUpdateRequest,
            "initial sync queued unsupported desktop properties");
        view.flushSystemPropertyUpdatesImmediate();
        requireDesktopCalls(view, 0, "unsupported desktop API was invoked");
        require(view.backgroundCalls == 1, "full-screen supported property was lost");
    });
    test("ordinary Pad setter skips desktop calls", [] {
        QOhosView view;
        view.setSystemUpdateProperty(&QOhosView::SystemUpdateData::title, std::string("changed"));
        view.setSystemUpdateProperty(&QOhosView::SystemUpdateData::windowMinMaxCloseButtonsState, 0);
        view.setSystemUpdateProperty(&QOhosView::SystemUpdateData::frameless, true);
        view.flushSystemPropertyUpdatesImmediate();
        requireDesktopCalls(view, 0, "setter invoked unsupported desktop API");
    });
    test("PC main window preserves initial and later title/buttons/frameless", [] {
        QOhosSettings::instance().pcMode = true;
        QOhosView view;
        view.initialSync();
        view.flushSystemPropertyUpdatesImmediate();
        requireDesktopCalls(view, 1, "PC initial desktop operation was dropped");
        view.setSystemUpdateProperty(&QOhosView::SystemUpdateData::title, std::string("changed"));
        view.setSystemUpdateProperty(&QOhosView::SystemUpdateData::windowMinMaxCloseButtonsState, 0);
        view.setSystemUpdateProperty(&QOhosView::SystemUpdateData::frameless, true);
        view.flushSystemPropertyUpdatesImmediate();
        requireDesktopCalls(view, 2, "PC later desktop operation was dropped");
        require(view.appliedTitle == "changed", "PC title value was not applied");
    });
    test("pending PC update is discarded after switching to ordinary Pad", [] {
        QOhosSettings::instance().pcMode = true;
        QOhosView view;
        view.initialSync();
        QOhosSettings::instance().pcMode = false;
        view.flushSystemPropertyUpdatesImmediate();
        requireDesktopCalls(view, 0, "queued PC operation ran after mode changed");
        require(view.backgroundCalls == 1 && !view.m_updatePending,
            "supported update or flush completion was lost");
        QOhosSettings::instance().pcMode = true;
        view.flushSystemPropertyUpdatesImmediate();
        requireDesktopCalls(view, 0, "discarded operation survived in pending queue");
    });
    test("ordinary Pad initial sync clears an obsolete pending PC title", [] {
        QOhosSettings::instance().pcMode = true;
        QOhosView view;
        view.setSystemUpdateProperty(&QOhosView::SystemUpdateData::title, std::string("obsolete"));
        QOhosSettings::instance().pcMode = false;
        view.initialSync();
        require(!view.m_updateData.title.optPendingUpdateRequest,
            "initial sync retained an unsupported pending title");
    });
    test("existing subwindow mode exception is preserved", [] {
        QOhosView view;
        view.type = ViewType::SubWindow;
        view.initialSync();
        view.flushSystemPropertyUpdatesImmediate();
        requireDesktopCalls(view, 1, "existing subwindow operation was changed");
    });
    return failures ? 1 : 0;
}
"""
    for key, value in (("SUPPORT_DECL", support_decl), ("SUPPORT_IMPL", support_impl),
                       ("SETTER", setter), ("FLUSH", flush), ("SUBMIT", submit)):
        harness = harness.replace(key, value)
    temp_root = root / "tmp/qt-window-mode-test"
    temp_root.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env["PATH"] = str(args.cxx.parent) + os.pathsep + env.get("PATH", "")
    env["TEMP"] = str(temp_root)
    env["TMP"] = str(temp_root)
    try:
        with tempfile.TemporaryDirectory(dir=temp_root) as work:
            work = Path(work)
            cpp = work / "window-mode-sync.cpp"
            exe = work / "window-mode-sync.exe"
            cpp.write_text(harness, encoding="utf-8")
            subprocess.run([str(args.cxx), "-std=c++20", "-Wall", "-Wextra",
                            "-static-libgcc", "-static-libstdc++", str(cpp), "-o", str(exe)],
                           check=True, env=env)
            result = subprocess.run([str(exe)], env=env)
            raise SystemExit(result.returncode)
    finally:
        temp_root.rmdir()


if __name__ == "__main__":
    main()

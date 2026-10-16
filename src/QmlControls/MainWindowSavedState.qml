/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtCore

import QGroundControl
import QGroundControl.ScreenTools

Item {
    property Window window

    property bool _enabled: !ScreenTools.isMobile && !ScreenTools.isFakeMobile && QGroundControl.corePlugin.options.enableSaveMainWindowPosition

    Settings {
        id:         s
        category:   "MainWindowState"
    }

    function _setDefaultDesktopWindowSize() {
        window.width = Math.min(250 * Screen.pixelDensity, Screen.width);
        window.height = Math.min(150 * Screen.pixelDensity, Screen.height);
    }

    function restoreSettings() {
        var visibility = Window.Windowed
        if (ScreenTools.isFakeMobile) {
            window.width = ScreenTools.screenWidth
            window.height = ScreenTools.screenHeight
        } else if (ScreenTools.isMobile) {
            visibility = Window.FullScreen
        } else if (QGroundControl.corePlugin.options.enableSaveMainWindowPosition) {
            if (!ScreenTools.isHarmonyTablet) {
                window.minimumWidth = Math.min(ScreenTools.defaultFontPixelWidth * 100, Screen.width)
                window.minimumHeight = Math.min(ScreenTools.defaultFontPixelWidth * 50, Screen.height)
            }
            var geometry = Qt.rect(s.value("x", 0), s.value("y", 0), s.value("width", 0), s.value("height", 0))
            if (geometry.width > 0 && geometry.height > 0) {
                window.x = geometry.x;
                window.y = geometry.y;
                window.width = geometry.width;
                window.height = geometry.height;
                visibility = Number(s.value("visibility", Window.Windowed));
                if (visibility !== Window.Windowed && visibility !== Window.Maximized && visibility !== Window.FullScreen) {
                    visibility = Window.Windowed
                }
            } else if (ScreenTools.isHarmonyTablet) {
                visibility = Window.FullScreen
            } else {
                _setDefaultDesktopWindowSize()
            }
        } else if (ScreenTools.isHarmonyTablet) {
            visibility = Window.FullScreen
        } else {
            _setDefaultDesktopWindowSize()
        }
        return visibility
    }

    Connections {
        target:                         window
        function onXChanged()           { if(_enabled) saveSettingsTimer.restart() }
        function onYChanged()           { if(_enabled) saveSettingsTimer.restart() }
        function onWidthChanged()       { if(_enabled) saveSettingsTimer.restart() }
        function onHeightChanged()      { if(_enabled) saveSettingsTimer.restart() }
        function onVisibilityChanged()  { if(_enabled) saveSettingsTimer.restart() }
        function onClosing() {
            if (_enabled) {
                saveSettingsTimer.stop()
                saveSettings()
                s.sync()
            }
        }
    }

    Timer {
        id:             saveSettingsTimer
        interval:       500
        repeat:         false
        onTriggered:    saveSettings()
    }

    function saveSettings() {
        if (_enabled) {
            switch(window.visibility) {
            case ApplicationWindow.Windowed:
                s.setValue("x", window.x);
                s.setValue("y", window.y);
                s.setValue("width", window.width);
                s.setValue("height", window.height);
                s.setValue("visibility", window.visibility);
                break;
            case ApplicationWindow.FullScreen:
            case ApplicationWindow.Maximized:
                s.setValue("visibility", window.visibility);
                break;
            }
        }
    }
}

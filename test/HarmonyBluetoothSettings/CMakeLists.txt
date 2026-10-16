cmake_minimum_required(VERSION 3.25)
project(HarmonyBluetoothSettingsTest LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Test)

foreach(pair "QGCButton|Button" "QGCLabel|Label" "QGCComboBox|ComboBox")
    string(REPLACE "|" ";" fields "${pair}")
    list(GET fields 0 name)
    list(GET fields 1 base)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${name}.qml" "import QtQuick\nimport QtQuick.Controls\n${base} {}\n")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/QGCTextField.qml" "import QtQuick.Controls\nTextField { property bool validationError: false }\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ScreenTools.qml" "pragma Singleton\nimport QtQml\nQtObject { property real defaultFontPixelWidth: 8; property real defaultFontPixelHeight: 16; property bool isMobile: false }\n")
add_executable(HarmonyBluetoothSettingsTest BluetoothSettingsTest.cc)
target_link_libraries(HarmonyBluetoothSettingsTest PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
target_compile_definitions(HarmonyBluetoothSettingsTest PRIVATE
    PRODUCT_QML_PATH="${CMAKE_CURRENT_LIST_DIR}/../../src/UI/AppSettings/BluetoothSettings.qml"
    PRODUCT_LINK_QML_PATH="${CMAKE_CURRENT_LIST_DIR}/../../src/UI/AppSettings/LinkSettings.qml"
    TEST_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}")
enable_testing()
add_test(NAME HarmonyBluetoothSettingsTest COMMAND HarmonyBluetoothSettingsTest
    -o "${CMAKE_CURRENT_BINARY_DIR}/result.txt,txt"
    -o "${CMAKE_CURRENT_BINARY_DIR}/result.xml,junitxml")
set_tests_properties(HarmonyBluetoothSettingsTest PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic;QML_DISABLE_DISK_CACHE=1")

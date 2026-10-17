cmake_minimum_required(VERSION 3.25)
project(AppSettingsWarningsTest LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick)

foreach(pair "QGCButton|Button" "QGCLabel|Label" "QGCComboBox|ComboBox" "QGCCheckBox|CheckBox" "QGCTextField|TextField" "QGCTextArea|TextArea" "QGCFlickable|Flickable" "QGCListView|ListView")
    string(REPLACE "|" ";" fields "${pair}")
    list(GET fields 0 name)
    list(GET fields 1 base)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${name}.qml" "import QtQuick\nimport QtQuick.Controls\n${base} {}\n")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/Fact.qml" "import QtQml\nQtObject { property bool rawValue: false }\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/QGCPalette.qml" "import QtQuick\nQtObject { property color window: '#222222'; property color windowShade: '#333333'; property color text: 'white'; property color textFieldText: 'white'; property color warningText: 'yellow' }\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ScreenTools.qml" "pragma Singleton\nimport QtQml\nQtObject { property real defaultFontPixelWidth: 8; property real defaultFontPixelHeight: 16; property real defaultFontPointSize: 12 }\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/QGroundControl.qml" "pragma Singleton
import QtQml
import QGroundControl.FactSystem
QtObject {
    property bool apmFirmwareSupported: true
    property var corePlugin: QtObject { property var options: QtObject { property bool showPX4LogTransferOptions: true } }
    property var multiVehicleManager: QtObject { property var activeVehicle: null }
    property var settingsManager: QtObject {
        property var appSettings: QtObject { property Fact disableAllPersistence: Fact {} }
        property var apmMavlinkStreamRateSettings: QtObject { property bool visible: true }
    }
}")

add_executable(AppSettingsWarningsTest AppSettingsWarningsTest.cc)
target_link_libraries(AppSettingsWarningsTest PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
target_compile_definitions(AppSettingsWarningsTest PRIVATE
    PRODUCT_QML_PATH="${CMAKE_CURRENT_LIST_DIR}/../../src/UI/AppSettings/PX4LogTransferSettings.qml"
    TEST_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}")
enable_testing()
add_test(NAME AppSettingsWarningsTest COMMAND AppSettingsWarningsTest)
set_tests_properties(AppSettingsWarningsTest PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic;QML_DISABLE_DISK_CACHE=1")

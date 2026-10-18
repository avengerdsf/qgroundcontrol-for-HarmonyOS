cmake_minimum_required(VERSION 3.25)
project(AppSettingsWarningsTest LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick)

foreach(component PX4LogControl PX4LogUploadSettings PX4LogFileManager)
    configure_file("../../src/AppSettings/${component}.qml" "${component}.qml" COPYONLY)
endforeach()
configure_file("../../src/QmlControls/LabelledComboBox.qml" "LabelledComboBox.qml" COPYONLY)
foreach(pair "QGCButton|Button" "QGCLabel|Label" "QGCCheckBox|CheckBox" "QGCCheckBoxSlider|CheckBox"
             "QGCTextField|TextField" "QGCTextArea|TextArea" "QGCFlickable|Flickable" "QGCListView|ListView")
    string(REPLACE "|" ";" fields "${pair}")
    list(GET fields 0 name)
    list(GET fields 1 base)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${name}.qml" "import QtQuick\nimport QtQuick.Controls\n${base} {}\n")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/QGCComboBox.qml" "import QtQuick.Controls\nComboBox { property bool sizeToContents: false; property string alternateText }\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/SettingsGroupLayout.qml" "import QtQuick.Layouts\nColumnLayout { property string heading }\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/MessageDialog.qml" "import QtQuick\nItem { enum StandardButton { Yes = 16384, No = 65536, Close = 2097152 } property int buttons; property string title; property string text; signal buttonClicked(int button, int role); function open() { visible = true } }\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/QGCPalette.qml" "import QtQuick\nQtObject { property color window: '#222222'; property color windowShade: '#333333'; property color text: 'white'; property color textFieldText: 'white'; property color textField: 'black'; property color warningText: 'yellow' }\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ScreenTools.qml" "pragma Singleton\nimport QtQml\nQtObject { property real defaultFontPixelWidth: 8; property real defaultFontPixelHeight: 16; property real defaultFontPointSize: 12 }\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/QGroundControl.qml" "pragma Singleton
import QtQml
QtObject {
    property var corePlugin: QtObject { property var options: QtObject { property bool showPX4LogTransferOptions: true } }
    property var multiVehicleManager: QtObject { property var activeVehicle: null }
    property var settingsManager: QtObject {
        property var appSettings: QtObject { property var disableAllPersistence: QtObject { property bool rawValue: false } }
    }
}")
add_executable(AppSettingsWarningsTest AppSettingsWarningsTest.cc)
target_link_libraries(AppSettingsWarningsTest PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
target_compile_definitions(AppSettingsWarningsTest PRIVATE TEST_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}")
enable_testing()
add_test(NAME AppSettingsWarningsTest COMMAND AppSettingsWarningsTest)

if(WIN32)
    set_property(TEST AppSettingsWarningsTest APPEND PROPERTY ENVIRONMENT "QT_QPA_FONTDIR=$ENV{WINDIR}/Fonts")
endif()

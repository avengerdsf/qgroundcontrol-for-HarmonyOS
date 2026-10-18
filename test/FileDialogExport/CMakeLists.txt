cmake_minimum_required(VERSION 3.25)
project(FileDialogExportTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Test)

# Load production logging and file-dialog components with visual-control
# and application-service fixtures.
configure_file("../../src/AppSettings/AppLogging.qml" "AppLogging.qml" COPYONLY)
foreach(file QGCFileDialog.qml QGCPopupDialogFactory.qml)
    configure_file("../../src/QmlControls/${file}" "${file}" COPYONLY)
endforeach()
foreach(pair "QGCButton|Button" "QGCLabel|Label" "QGCTextField|TextField"
             "QGCCheckBox|CheckBox" "QGCMenu|Menu" "QGCMenuItem|MenuItem"
             "QGCListView|ListView" "SectionHeader|Label" "QGCFlickable|Flickable" "QGCMouseArea|MouseArea")
    string(REPLACE "|" ";" fields "${pair}")
    list(GET fields 0 name)
    list(GET fields 1 base)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${name}.qml" "import QtQuick\nimport QtQuick.Controls\n${base} {}\n")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/QGCComboBox.qml" "import QtQuick.Controls\nComboBox { property bool sizeToContents: false }\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/LoggingCategoriesDialog.qml" "import QtQuick\nItem {}\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/FileButton.qml" "import QtQuick.Controls\nButton { property bool highlight: false; signal hamburgerClicked() }\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/FactComboBox.qml" "import QtQuick.Controls\nComboBox { property var fact; property bool sizeToContents: false }\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/QGCPopupDialog.qml" "import QtQuick.Controls\nDialog { property bool preventClose: false; property var buttons; property bool destroyOnClose: true }\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/QGCPalette.qml" "import QtQuick\nQtObject { property bool colorGroupEnabled: true; property color window: 'white'; property color windowShade: 'gray'; property color warningText: 'red'; property color text: 'black'; property color colorGrey: 'gray'; property color colorOrange: 'orange'; property color colorRed: 'red'; property color groupBorder: 'gray' }\n")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/stubs")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/stubs/SettingsManager.h" "")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/stubs/AppSettings.h" "")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/stubs/QGCLoggingCategory.h" "#include <QtCore/QLoggingCategory>\n#define QGC_LOGGING_CATEGORY Q_LOGGING_CATEGORY\n")

add_executable(FileDialogExportTest FileDialogExportTest.cc
    ../../src/QmlControls/QGCFileDialogController.cc
    ../../src/QmlControls/QGCFileDialogController.h
    ../../src/LogManager/LogEntry.cc ../../src/LogManager/LogEntry.h)
target_include_directories(FileDialogExportTest PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/stubs" ../../src/QmlControls ../../src/LogManager)
target_link_libraries(FileDialogExportTest PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
target_compile_definitions(FileDialogExportTest PRIVATE
    TEST_SOURCE_DIR="${CMAKE_CURRENT_LIST_DIR}"
    TEST_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}")

enable_testing()
add_test(NAME FileDialogExportTest COMMAND FileDialogExportTest -o "${CMAKE_BINARY_DIR}/result.txt,txt")
set_tests_properties(FileDialogExportTest PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic;QML_DISABLE_DISK_CACHE=1")

if(WIN32)
    set_property(TEST FileDialogExportTest APPEND PROPERTY ENVIRONMENT "QT_QPA_FONTDIR=$ENV{WINDIR}/Fonts")
endif()

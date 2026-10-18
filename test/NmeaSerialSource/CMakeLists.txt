cmake_minimum_required(VERSION 3.25)
project(NmeaSerialSourceTest VERSION 5.1.5 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Network SerialPort Positioning Test)
if(OHOS)
    if(NOT OHOS_ARCH STREQUAL "x86_64")
        message(FATAL_ERROR "The NMEA runtime test package supports only x86_64 emulators")
    endif()
    find_package(Qt6 6.12 REQUIRED COMPONENTS CorePrivate Gui)
    option(QGC_ENABLE_BLUETOOTH "Stage Bluetooth factories in the NMEA test package" OFF)
    option(QGC_NO_SERIAL_LINK "Exclude USB ETS factories from the controlled-provider test package" ON)
    if(QGC_ENABLE_BLUETOOTH OR NOT QGC_NO_SERIAL_LINK)
        message(FATAL_ERROR "The NMEA runtime package must exclude Bluetooth and USB ETS factories")
    endif()
endif()
set(root "${CMAKE_CURRENT_LIST_DIR}/../..")
set(link_file "${root}/src/Comms/LinkManager.cc")
set(position_file "${root}/src/PositionManager/PositionManager.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${link_file}" "${position_file}")
function(section source first last output)
    string(FIND "${source}" "${first}" start)
    if(start LESS 0)
        message(FATAL_ERROR "Missing production boundary ${first}")
    endif()
    string(SUBSTRING "${source}" ${start} -1 remaining)
    string(FIND "${remaining}" "${last}" end)
    if(end LESS 0)
        message(FATAL_ERROR "Missing production boundary ${last}")
    endif()
    string(SUBSTRING "${remaining}" 0 ${end} body)
    set(${output} "${body}" PARENT_SCOPE)
endfunction()
file(READ "${link_file}" link)
file(READ "${position_file}" position)
section("${link}" "void LinkManager::_addSerialAutoConnectLink()" "bool LinkManager::_allowAutoConnectToBoard" serial)
section("${link}" "void LinkManager::_updateAutoConnectLinks()" "void LinkManager::shutdown()" update)
section("${position}" "void QGCPositionManager::setNmeaSourceDevice(" "void QGCPositionManager::resetNmeaSourceDevice(" setter)
string(FIND "${link}" "class HarmonyNmeaIODevice final" adapter_start)
set(adapter "")
set(port_type QSerialPort)
if(NOT adapter_start LESS 0)
    section("${link}" "class HarmonyNmeaIODevice final" "\n#endif" adapter)
    set(port_type HarmonyNmeaIODevice)
endif()
configure_file(Fixture.h.in Fixture.h @ONLY)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/Production.cc"
    "#include \"Fixture.h\"\n${adapter}\n${setter}\n${update}\n${serial}\nQIODevice *LinkManager::nmeaDevice() const { return _nmeaPort; }\nLinkManager::~LinkManager() { delete _nmeaPort; }\n")
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/Production.cc" PROPERTIES COMPILE_DEFINITIONS Q_OS_HARMONY)
set(test_sources NmeaSerialSourceTest.cc "${CMAKE_CURRENT_BINARY_DIR}/Fixture.h"
    "${CMAKE_CURRENT_BINARY_DIR}/Production.cc" "${root}/src/Comms/HarmonySerialBridge.cc"
    "${root}/src/Comms/HarmonySerialBridge.h" "${root}/src/Comms/UdpIODevice.cc" "${root}/src/Comms/UdpIODevice.h")
if(OHOS)
    set(test_target QGroundControl)
    qt_add_executable(${test_target} ${test_sources})
    get_filename_component(workspace "${root}" ABSOLUTE)
    find_path(NodeAddonApi_INCLUDE_DIR NAMES napi.h HINTS "${workspace}/tmp/vcpkg/installed/x64-ohos/include"
        NO_CMAKE_FIND_ROOT_PATH REQUIRED)
    include("${Qt6_DIR}/FindNodeAddonApi.cmake")
    target_link_libraries(${test_target} PRIVATE Qt6::CorePrivate Qt6::Gui NodeAddonApi::NodeAddonApi ace_napi.z)
    target_compile_definitions(${test_target} PRIVATE NODE_ADDON_API_ENABLE_TYPE_CHECK_ON_AS NAPI_CPP_EXCEPTIONS)
    set_target_properties(${test_target} PROPERTIES
        QT_HARMONYOS_APP_BUNDLE_NAME "org.mavlink.qgc.nmea.tests"
        QT_HARMONYOS_APP_NAME "QGroundControl"
        QT_HARMONYOS_APP_LABEL "QGC NMEA Tests"
        QT_HARMONYOS_APP_VENDOR "QGroundControl"
        QT_HARMONYOS_APP_ICON "${workspace}/resources/icons/android_512x512.png"
        QT_HARMONYOS_APP_VERSION_NAME "5.1.5"
        QT_HARMONYOS_APP_VERSION_CODE 5010513
        QT_HARMONYOS_MODULE_DEVICE_TYPES "tablet;2in1"
        QT_HARMONYOS_COMPATIBLE_SDK_VERSION "6.1.0(23)"
        QT_HARMONYOS_TARGET_ARCHS "x86_64"
        QT_QML_ROOT_PATH "${CMAKE_CURRENT_SOURCE_DIR}")
    qt_add_harmonyos_permission(${test_target} NAME "ohos.permission.INTERNET")
else()
    set(test_target NmeaSerialSourceTest)
    add_executable(${test_target} ${test_sources})
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/QGCLoggingCategory.h" "#include <QtCore/QLoggingCategory>\n#define QGC_LOGGING_CATEGORY Q_LOGGING_CATEGORY\n#define QGC_LOGGING_CATEGORY_ON Q_LOGGING_CATEGORY\n")
target_include_directories(${test_target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${root}/src/Comms")
target_compile_definitions(${test_target} PRIVATE QGC_UNITTEST_BUILD)
target_link_libraries(${test_target} PRIVATE Qt6::Core Qt6::Network Qt6::SerialPort Qt6::Positioning Qt6::Test)
set(snapshot_material "")
foreach(path "${link_file}" "${root}/src/Comms/LinkManager.h" "${position_file}"
        "${root}/src/Comms/HarmonySerialBridge.cc" "${root}/src/Comms/HarmonySerialBridge.h"
        "${root}/src/Comms/UdpIODevice.cc" "${root}/src/Comms/UdpIODevice.h"
        "${CMAKE_CURRENT_LIST_FILE}" "${CMAKE_CURRENT_LIST_DIR}/Fixture.h.in"
        "${CMAKE_CURRENT_LIST_DIR}/NmeaSerialSourceTest.cc")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
    file(SHA256 "${path}" hash)
    string(APPEND snapshot_material "${hash}\n")
endforeach()
string(SHA256 source_snapshot "${snapshot_material}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/nmea-source-snapshot.txt" "${source_snapshot}\n")
target_compile_definitions(${test_target} PRIVATE TEST_SOURCE_SNAPSHOT="${source_snapshot}")
if(NOT OHOS)
    enable_testing()
    add_test(NAME NmeaSerialSourceTest COMMAND NmeaSerialSourceTest
        -o "${CMAKE_CURRENT_BINARY_DIR}/result.txt,txt" -o "${CMAKE_CURRENT_BINARY_DIR}/result.xml,junitxml")
    set_tests_properties(NmeaSerialSourceTest PROPERTIES TIMEOUT 30)
endif()

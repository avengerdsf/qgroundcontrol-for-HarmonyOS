cmake_minimum_required(VERSION 3.25)
project(QGroundControl VERSION 5.0.8 LANGUAGES CXX)
if(NOT OHOS)
    message(FATAL_ERROR "This target requires the HarmonyOS Qt toolchain.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(QGC_ENABLE_BLUETOOTH "Stage the production Bluetooth factories" ON)
option(QGC_NO_SERIAL_LINK "Exclude USB serial from the runtime probe" ON)
get_filename_component(QGC_WORKSPACE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
find_package(Qt6 REQUIRED COMPONENTS Core CorePrivate Gui Test)
find_path(NodeAddonApi_INCLUDE_DIR NAMES napi.h
    HINTS "${QGC_WORKSPACE_ROOT}/tmp/vcpkg/installed/x64-ohos/include"
    NO_CMAKE_FIND_ROOT_PATH REQUIRED)
include("${Qt6_DIR}/FindNodeAddonApi.cmake")
qt_add_executable(QGroundControl RuntimeTest.cc
    "${QGC_WORKSPACE_ROOT}/src/Comms/HarmonyBluetoothBridge.cc"
    "${QGC_WORKSPACE_ROOT}/src/Comms/HarmonyBluetoothBridge.h")
target_include_directories(QGroundControl PRIVATE "${QGC_WORKSPACE_ROOT}/src/Comms")
target_link_libraries(QGroundControl PRIVATE
    Qt6::Core Qt6::CorePrivate Qt6::Gui Qt6::Test NodeAddonApi::NodeAddonApi ace_napi.z)
target_compile_definitions(QGroundControl PRIVATE NODE_ADDON_API_ENABLE_TYPE_CHECK_ON_AS NAPI_CPP_EXCEPTIONS)
set_target_properties(QGroundControl PROPERTIES
    QT_HARMONYOS_APP_BUNDLE_NAME "org.mavlink.qgc.runtime.tests"
    QT_HARMONYOS_APP_NAME "QGroundControl"
    QT_HARMONYOS_APP_LABEL "QGC Runtime Tests"
    QT_HARMONYOS_APP_VENDOR "QGroundControl"
    QT_HARMONYOS_APP_ICON "${QGC_WORKSPACE_ROOT}/resources/icons/android_512x512.png"
    QT_HARMONYOS_APP_VERSION_NAME "5.0.8"
    QT_HARMONYOS_APP_VERSION_CODE 5000800
    QT_HARMONYOS_MODULE_DEVICE_TYPES "tablet;2in1"
    QT_HARMONYOS_COMPATIBLE_SDK_VERSION "6.1.0(23)"
    QT_HARMONYOS_TARGET_ARCHS "${OHOS_ARCH}"
    QT_QML_ROOT_PATH "${CMAKE_CURRENT_SOURCE_DIR}")
qt_add_harmonyos_permission(QGroundControl NAME "ohos.permission.ACCESS_BLUETOOTH"
    REASON "$string:qgc_bluetooth_reason" USED_SCENE_ABILITIES "QAbility" USED_SCENE_WHEN "inuse")

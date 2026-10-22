cmake_minimum_required(VERSION 3.25)
project(HarmonyBluetoothConfigurationTest LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Test Bluetooth Concurrent)
set(comms "${CMAKE_CURRENT_LIST_DIR}/../../src/Comms")
configure_file(LinkConfiguration.h.in LinkConfiguration.h COPYONLY)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/QGCLoggingCategory.h" "#pragma once\n#include <QtCore/QLoggingCategory>\n#define QGC_LOGGING_CATEGORY Q_LOGGING_CATEGORY\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/QGCNetworkHelper.h" "#pragma once\nnamespace QGCNetworkHelper { inline bool isBluetoothAvailable() { return true; } }\n")
set(sources "${comms}/Bluetooth/BluetoothConfiguration.cc" "${comms}/Bluetooth/BluetoothWorker.cc")
if(EXISTS "${comms}/Bluetooth/HarmonyBluetoothWorker.cc")
    list(APPEND sources "${comms}/Bluetooth/HarmonyBluetoothWorker.cc")
endif()
set_source_files_properties(${sources} PROPERTIES COMPILE_DEFINITIONS Q_OS_HARMONY)
add_executable(HarmonyBluetoothConfigurationTest ConfigurationTest.cc ${sources}
    "${comms}/Bluetooth/BluetoothConfiguration.h" "${comms}/Bluetooth/BluetoothWorker.h"
    "${comms}/Bluetooth/BluetoothClassicWorker.cc" "${comms}/Bluetooth/BluetoothBleWorker.cc"
    "${comms}/HarmonyBluetoothBridge.cc" "${comms}/HarmonyBluetoothBridge.h"
    "${CMAKE_CURRENT_BINARY_DIR}/LinkConfiguration.h")
target_compile_definitions(HarmonyBluetoothConfigurationTest PRIVATE QGC_UNITTEST_BUILD TEST_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(HarmonyBluetoothConfigurationTest PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${comms}/Bluetooth" "${comms}")
target_link_libraries(HarmonyBluetoothConfigurationTest PRIVATE Qt6::Core Qt6::Test Qt6::Bluetooth Qt6::Concurrent)
enable_testing()
add_test(NAME HarmonyBluetoothConfigurationTest COMMAND HarmonyBluetoothConfigurationTest -o "${CMAKE_CURRENT_BINARY_DIR}/result.txt,txt" -o "${CMAKE_CURRENT_BINARY_DIR}/result.xml,junitxml")
set_tests_properties(HarmonyBluetoothConfigurationTest PROPERTIES TIMEOUT 30)

configure_file("${comms}/Bluetooth/BluetoothConfiguration.cc" NativeBluetoothConfiguration.cc COPYONLY)
add_executable(NativeBluetoothConfigurationTest NativeConfigurationTest.cc
    "${CMAKE_CURRENT_BINARY_DIR}/NativeBluetoothConfiguration.cc"
    "${comms}/Bluetooth/BluetoothConfiguration.h"
    "${CMAKE_CURRENT_BINARY_DIR}/LinkConfiguration.h")
target_include_directories(NativeBluetoothConfigurationTest PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}" "${comms}/Bluetooth" "${comms}")
target_link_libraries(NativeBluetoothConfigurationTest PRIVATE Qt6::Core Qt6::Test Qt6::Bluetooth Qt6::Concurrent)
add_test(NAME NativeBluetoothConfigurationTest COMMAND NativeBluetoothConfigurationTest
    -o "${CMAKE_CURRENT_BINARY_DIR}/native-result.txt,txt"
    -o "${CMAKE_CURRENT_BINARY_DIR}/native-result.xml,junitxml")
set_tests_properties(NativeBluetoothConfigurationTest PROPERTIES TIMEOUT 30)

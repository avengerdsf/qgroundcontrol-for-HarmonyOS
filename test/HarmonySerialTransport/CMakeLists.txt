cmake_minimum_required(VERSION 3.25)
project(HarmonySerialTransportTest LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Test)
add_executable(HarmonySerialTransportTest
    HarmonySerialTransportTest.cc
    ../../src/Comms/HarmonySerialBridge.cc
    ../../src/Comms/HarmonySerialBridge.h)
target_compile_definitions(HarmonySerialTransportTest PRIVATE QGC_UNITTEST_BUILD)
target_include_directories(HarmonySerialTransportTest PRIVATE . ../../src/Comms)
target_link_libraries(HarmonySerialTransportTest PRIVATE Qt6::Core Qt6::Test)
enable_testing()
add_test(NAME HarmonySerialTransportTest COMMAND HarmonySerialTransportTest
    -o "${CMAKE_CURRENT_BINARY_DIR}/result.txt,txt"
    -o "${CMAKE_CURRENT_BINARY_DIR}/result.xml,junitxml")
set_tests_properties(HarmonySerialTransportTest PROPERTIES TIMEOUT 30
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

add_executable(SerialGPSTransportHostTest SerialGPSTransportHostTest.cc
    ../../src/Comms/HarmonySerialBridge.cc ../../src/Comms/HarmonySerialBridge.h
    ../../src/Comms/HarmonySerialIODevice.cc ../../src/Comms/HarmonySerialIODevice.h
    ../../src/GPS/SerialGPSTransport.cc ../../src/GPS/SerialGPSTransport.h)
set_source_files_properties(SerialGPSTransportHostTest.cc ../../src/GPS/SerialGPSTransport.cc
    PROPERTIES COMPILE_DEFINITIONS Q_OS_HARMONY)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/QGCLoggingCategory.h"
    "#include <QtCore/QLoggingCategory>\n#define QGC_LOGGING_CATEGORY Q_LOGGING_CATEGORY\n#define QGC_LOGGING_CATEGORY_ON Q_LOGGING_CATEGORY\n")
target_compile_definitions(SerialGPSTransportHostTest PRIVATE QGC_UNITTEST_BUILD)
target_include_directories(SerialGPSTransportHostTest PRIVATE . "${CMAKE_CURRENT_BINARY_DIR}"
    ../../src/Comms ../../src/GPS ../../src/GPS/Driver)
target_link_libraries(SerialGPSTransportHostTest PRIVATE Qt6::Core Qt6::Test)
add_test(NAME SerialGPSTransportHostTest COMMAND SerialGPSTransportHostTest
    -o "${CMAKE_CURRENT_BINARY_DIR}/gps-result.txt,txt" -o "${CMAKE_CURRENT_BINARY_DIR}/gps-result.xml,junitxml")
set_tests_properties(SerialGPSTransportHostTest PROPERTIES TIMEOUT 15)

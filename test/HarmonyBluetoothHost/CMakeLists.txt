cmake_minimum_required(VERSION 3.25)
project(HarmonyBluetoothHostTest LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Test)
add_executable(HarmonyBluetoothHostTest
    HarmonyBluetoothHostTest.cc
    ../../src/Comms/HarmonyBluetoothBridge.cc
    ../../src/Comms/HarmonyBluetoothBridge.h
)
target_compile_definitions(HarmonyBluetoothHostTest PRIVATE QGC_UNITTEST_BUILD)
target_include_directories(HarmonyBluetoothHostTest PRIVATE ../../src/Comms)
target_link_libraries(HarmonyBluetoothHostTest PRIVATE Qt6::Core Qt6::Test)
enable_testing()
add_test(NAME HarmonyBluetoothHostTest COMMAND HarmonyBluetoothHostTest
    -o "${CMAKE_CURRENT_BINARY_DIR}/result.txt,txt"
    -o "${CMAKE_CURRENT_BINARY_DIR}/result.xml,junitxml")

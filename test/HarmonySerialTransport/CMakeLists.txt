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

cmake_minimum_required(VERSION 3.25)
project(HarmonyBluetoothConfigurationTest LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Test Bluetooth)

function(read_section path first last output)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
    file(READ "${path}" source)
    string(FIND "${source}" "${first}" start)
    if(start LESS 0)
        message(FATAL_ERROR "Missing production section: ${first}")
    endif()
    string(SUBSTRING "${source}" ${start} -1 remainder)
    string(FIND "${remainder}" "${last}" end)
    if(end LESS 0)
        message(FATAL_ERROR "Missing production boundary: ${last}")
    endif()
    string(SUBSTRING "${remainder}" 0 ${end} section)
    set(${output} "${section}" PARENT_SCOPE)
endfunction()

set(comms "${CMAKE_CURRENT_LIST_DIR}/../../src/Comms")
read_section("${comms}/BluetoothLink.h" "struct BluetoothData" "class BluetoothConfiguration" bluetooth_data)
read_section("${comms}/BluetoothLink.h" "class BluetoothConfiguration :" "class BluetoothWorker :" configuration_header)
read_section("${comms}/BluetoothLink.h" "class BluetoothWorker :" "class BluetoothLink :" worker_header)
read_section("${comms}/BluetoothLink.cc" "namespace {" "BluetoothLink::BluetoothLink" configuration_source)
configure_file(Configuration.h.in Configuration.h @ONLY)
configure_file(Configuration.cc.in Configuration.cc @ONLY)

add_executable(HarmonyBluetoothConfigurationTest ConfigurationTest.cc
    "${CMAKE_CURRENT_BINARY_DIR}/Configuration.h" "${CMAKE_CURRENT_BINARY_DIR}/Configuration.cc"
    "${comms}/HarmonyBluetoothBridge.cc" "${comms}/HarmonyBluetoothBridge.h")
target_compile_definitions(HarmonyBluetoothConfigurationTest PRIVATE QGC_UNITTEST_BUILD
    TEST_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(HarmonyBluetoothConfigurationTest PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${comms}")
target_link_libraries(HarmonyBluetoothConfigurationTest PRIVATE Qt6::Core Qt6::Test Qt6::Bluetooth)

enable_testing()
add_test(NAME HarmonyBluetoothConfigurationTest COMMAND HarmonyBluetoothConfigurationTest
    -o "${CMAKE_CURRENT_BINARY_DIR}/result.txt,txt"
    -o "${CMAKE_CURRENT_BINARY_DIR}/result.xml,junitxml")
set_tests_properties(HarmonyBluetoothConfigurationTest PROPERTIES TIMEOUT 30)

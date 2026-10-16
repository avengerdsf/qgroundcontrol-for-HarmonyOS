cmake_minimum_required(VERSION 3.25)
project(SerialBaudRatesTest LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.12 REQUIRED COMPONENTS Core Test)
find_package(Git REQUIRED)

function(extract_section source first last output)
    string(FIND "${source}" "${first}" start)
    if(start LESS 0)
        message(FATAL_ERROR "Missing source section: ${first}")
    endif()
    string(SUBSTRING "${source}" ${start} -1 remainder)
    string(FIND "${remainder}" "${last}" end)
    if(end LESS 0)
        message(FATAL_ERROR "Missing source boundary: ${last}")
    endif()
    string(SUBSTRING "${remainder}" 0 ${end} section)
    set(${output} "${section}" PARENT_SCOPE)
endfunction()

get_filename_component(repository "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(production_path "${repository}/src/Comms/SerialLink.cc")
set(bridge_path "${repository}/src/Comms/HarmonySerialBridge.cc")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${production_path}" "${bridge_path}")
file(READ "${production_path}" production_source)
file(READ "${bridge_path}" bridge_source)
execute_process(COMMAND "${GIT_EXECUTABLE}" show v5.0.8:src/Comms/SerialLink.cc
    WORKING_DIRECTORY "${repository}" RESULT_VARIABLE git_result
    OUTPUT_VARIABLE reference_source ERROR_VARIABLE git_error)
if(NOT git_result EQUAL 0)
    message(FATAL_ERROR "Cannot read the v5.0.8 reference: ${git_error}")
endif()
execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse "v5.0.8^{commit}"
    WORKING_DIRECTORY "${repository}" OUTPUT_VARIABLE reference_revision OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
message(STATUS "Serial baud-rate reference: v5.0.8 ${reference_revision}")

set(signature "QStringList SerialConfiguration::supportedBaudRates()")
set(boundary "QString SerialConfiguration::cleanPortDisplayName")
extract_section("${production_source}" "${signature}" "${boundary}" production)
extract_section("${reference_source}" "${signature}" "${boundary}" reference)
string(REPLACE "SerialConfiguration::supportedBaudRates()" "UpstreamSerialConfiguration::supportedBaudRates()"
    reference "${reference}")
extract_section("${bridge_source}" "QList<qint32> HarmonySerialBridge::supportedBaudRates()"
    "void HarmonySerialBridge::_request" bridge)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/RatesFixture.h" [=[
#pragma once
#include <QtCore/QList>
#include <QtCore/QSet>
#include <QtCore/QStringList>
#include <algorithm>
#include <utility>

class QSerialPortInfo
{
public:
    static inline QList<qint32> qtRates;
    static inline int calls = 0;
    static QList<qint32> standardBaudRates() { ++calls; return qtRates; }
};

class HarmonySerialBridge
{
public:
    static QList<qint32> supportedBaudRates();
};

class SerialConfiguration
{
public:
    static QStringList supportedBaudRates();
};

class UpstreamSerialConfiguration
{
public:
    static QStringList supportedBaudRates();
};
]=])
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/RatesFixture.cc"
    "#include \"RatesFixture.h\"\n${production}\n${reference}\n${bridge}\n")

add_executable(SerialBaudRatesTest SerialBaudRatesTest.cc
    "${CMAKE_CURRENT_BINARY_DIR}/RatesFixture.h" "${CMAKE_CURRENT_BINARY_DIR}/RatesFixture.cc")
target_include_directories(SerialBaudRatesTest PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/RatesFixture.cc"
    PROPERTIES COMPILE_DEFINITIONS Q_OS_HARMONY)
target_link_libraries(SerialBaudRatesTest PRIVATE Qt6::Core Qt6::Test)

enable_testing()
add_test(NAME SerialBaudRatesTest COMMAND SerialBaudRatesTest
    -o "${CMAKE_CURRENT_BINARY_DIR}/result.txt,txt"
    -o "${CMAKE_CURRENT_BINARY_DIR}/result.xml,junitxml")
set_tests_properties(SerialBaudRatesTest PROPERTIES TIMEOUT 15)

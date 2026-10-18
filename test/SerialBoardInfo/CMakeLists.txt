cmake_minimum_required(VERSION 3.25)
project(SerialBoardInfoTest LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.12 REQUIRED COMPONENTS Core Test)

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

get_filename_component(repository "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(info_source "${repository}/src/Comms/QGCSerialPortInfo.cc")
set(plugin_source "${repository}/src/AutoPilotPlugins/APM/APMAutoPilotPlugin.cc")
read_section("${info_source}" "QGCSerialPortInfo::QGCSerialPortInfo()"
    "bool QGCSerialPortInfo::_loadJsonData()" constructors)
read_section("${info_source}" "QList<QGCSerialPortInfo> QGCSerialPortInfo::availablePorts()"
    "bool QGCSerialPortInfo::isBootloader()" enumeration)
read_section("${info_source}" "bool QGCSerialPortInfo::isBlackCube()"
    "bool QGCSerialPortInfo::isSystemPort" classification)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${plugin_source}")
file(READ "${plugin_source}" plugin)
string(FIND "${plugin}" "void APMAutoPilotPlugin::_checkForBadCubeBlack(bool parametersReady)" start)
if(start LESS 0)
    message(FATAL_ERROR "Missing production CubeBlack warning method")
endif()
string(SUBSTRING "${plugin}" ${start} -1 warning)
# This is the final method, inside the file's QGC_NO_SERIAL_LINK guard.
if(NOT warning MATCHES "\n#endif[\r\n \t]*$")
    message(FATAL_ERROR "Unexpected CubeBlack warning method boundary")
endif()
string(REGEX REPLACE "\n#endif[\r\n \t]*$" "\n" warning "${warning}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/BoardSource.cc"
    "#include \"BoardFixture.h\"\n#include <QtCore/QMutexLocker>\nQ_LOGGING_CATEGORY(QGCSerialPortInfoLog, \"qgc.test.serialboardinfo\")\n${constructors}\n${enumeration}\n${classification}\n${warning}\n")

add_executable(SerialBoardInfoTest SerialBoardInfoTest.cc BoardFixture.h
    "${CMAKE_CURRENT_BINARY_DIR}/BoardSource.cc")
target_include_directories(SerialBoardInfoTest PRIVATE "${CMAKE_CURRENT_LIST_DIR}" "${repository}/src/Comms")
target_compile_definitions(SerialBoardInfoTest PRIVATE Q_OS_HARMONY)
target_link_libraries(SerialBoardInfoTest PRIVATE Qt6::Core Qt6::Test)

enable_testing()
add_test(NAME SerialBoardInfoTest COMMAND SerialBoardInfoTest
    -o "${CMAKE_CURRENT_BINARY_DIR}/result.txt,txt"
    -o "${CMAKE_CURRENT_BINARY_DIR}/result.xml,junitxml")
set_tests_properties(SerialBoardInfoTest PROPERTIES TIMEOUT 15)

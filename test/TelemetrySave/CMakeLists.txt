cmake_minimum_required(VERSION 3.25)
project(TelemetrySaveTest LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.12 REQUIRED COMPONENTS Core Test)
set(production "${CMAKE_CURRENT_LIST_DIR}/../../src/Comms/MAVLinkProtocol.cc")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${production}")
file(READ "${production}" source)
string(FIND "${source}" "void MAVLinkProtocol::_saveTelemetryLog(" start)
string(FIND "${source}" "void MAVLinkProtocol::_vehicleCountChanged()" end)
if(start LESS 0 OR end LESS start)
    message(FATAL_ERROR "Cannot locate the production telemetry save functions")
endif()
math(EXPR length "${end} - ${start}")
string(SUBSTRING "${source}" ${start} ${length} functions)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/TelemetrySave.cc"
    "#include \"Fixture.h\"\n#define QFile ControlledFile\n#define QTemporaryFile ControlledTemporaryFile\n#define QDateTime ControlledDateTime\n${functions}\n")
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/TelemetrySave.cc"
    PROPERTIES COMPILE_DEFINITIONS Q_OS_HARMONY)
add_executable(TelemetrySaveTest TelemetrySaveTest.cc Fixture.h "${CMAKE_CURRENT_BINARY_DIR}/TelemetrySave.cc")
target_include_directories(TelemetrySaveTest PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
target_compile_definitions(TelemetrySaveTest PRIVATE
    TEST_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    BACKUP_DIR="${CMAKE_CURRENT_LIST_DIR}/../../tmp/verification/upload-pad-telemetry-backup")
target_link_libraries(TelemetrySaveTest PRIVATE Qt6::Core Qt6::Test)
enable_testing()
add_test(NAME TelemetrySaveTest COMMAND TelemetrySaveTest
    -o "${CMAKE_CURRENT_BINARY_DIR}/result.txt,txt"
    -o "${CMAKE_CURRENT_BINARY_DIR}/result.xml,junitxml")
set_tests_properties(TelemetrySaveTest PROPERTIES TIMEOUT 30)

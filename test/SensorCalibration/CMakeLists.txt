cmake_minimum_required(VERSION 3.25)
project(SensorCalibrationTest VERSION 5.0.8 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Quick Test)
get_filename_component(repository "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(mavlink_default "")
set(qgc_cache "${repository}/tmp/build-v5.0.8-harmonyos-arm64/CMakeCache.txt")
if(EXISTS "${qgc_cache}")
    file(STRINGS "${qgc_cache}" mavlink_cache REGEX "^CPM_PACKAGE_mavlink_SOURCE_DIR:INTERNAL=")
    string(REGEX REPLACE "^CPM_PACKAGE_mavlink_SOURCE_DIR:INTERNAL=" "" mavlink_default "${mavlink_cache}")
endif()
set(MAVLINK_INCLUDE_ROOT "${mavlink_default}" CACHE PATH "Existing generated QGC MAVLink headers (no download)")
if(NOT EXISTS "${MAVLINK_INCLUDE_ROOT}/all/mavlink.h")
    message(FATAL_ERROR "Set MAVLINK_INCLUDE_ROOT to the generated MAVLink headers used by QGC")
endif()
add_executable(SensorCalibrationTest SensorCalibrationTest.cc
    fixture/FactPanelController.h fixture/Vehicle.h
    "${repository}/src/AutoPilotPlugins/PX4/SensorsComponentController.cc"
    "${repository}/src/AutoPilotPlugins/PX4/SensorsComponentController.h"
    "${repository}/src/MAVLink/StatusTextHandler.cc"
    "${repository}/src/MAVLink/StatusTextHandler.h")
target_include_directories(SensorCalibrationTest PRIVATE fixture
    "${repository}/src/AutoPilotPlugins/PX4" "${repository}/src/MAVLink"
    "${MAVLINK_INCLUDE_ROOT}" "${MAVLINK_INCLUDE_ROOT}/all")
target_link_libraries(SensorCalibrationTest PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Test)
target_compile_definitions(SensorCalibrationTest PRIVATE
    CALIBRATION_REPLAY_PATH="${CMAKE_CURRENT_LIST_DIR}/accel-replay.json")
file(GLOB_RECURSE fixture_files "${CMAKE_CURRENT_LIST_DIR}/*")
set(manifest "{\n")
foreach(path IN LISTS fixture_files)
    if(NOT IS_DIRECTORY "${path}")
        list(APPEND source_files "${path}")
    endif()
endforeach()
list(APPEND source_files
    "${repository}/src/AutoPilotPlugins/PX4/SensorsComponentController.cc"
    "${repository}/src/AutoPilotPlugins/PX4/SensorsComponentController.h"
    "${repository}/src/MAVLink/StatusTextHandler.cc"
    "${repository}/src/MAVLink/StatusTextHandler.h"
    "${repository}/src/MAVLink/MAVLinkLib.h"
    "${repository}/src/MAVLink/QGCMAVLink.h"
    "${MAVLINK_INCLUDE_ROOT}/common/mavlink_msg_statustext.h"
    "${MAVLINK_INCLUDE_ROOT}/mavlink_helpers.h")
list(SORT source_files)
foreach(path IN LISTS source_files)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
    file(RELATIVE_PATH relative "${repository}" "${path}")
    file(SHA256 "${path}" hash)
    string(APPEND manifest "  \"${relative}\": \"${hash}\",\n")
endforeach()
string(REGEX REPLACE ",\n$" "\n}" manifest "${manifest}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/source-manifest.json" "${manifest}\n")
file(SHA256 "${CMAKE_CURRENT_BINARY_DIR}/source-manifest.json" source_snapshot)
target_compile_definitions(SensorCalibrationTest PRIVATE CALIBRATION_SOURCE_SNAPSHOT="${source_snapshot}")
enable_testing()
add_test(NAME SensorCalibrationTest COMMAND SensorCalibrationTest
    -o "${CMAKE_CURRENT_BINARY_DIR}/result.txt,txt"
    -o "${CMAKE_CURRENT_BINARY_DIR}/result.xml,junitxml")
set_tests_properties(SensorCalibrationTest PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

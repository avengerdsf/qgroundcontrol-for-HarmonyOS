# ============================================================================
# GPS Driver Module
# PX4 GPS driver integration + GNSS message structs
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        GPSDriver.cc
        GPSDriver.h
        GPSTransport.h
        GPSType.h
        definitions.h
        satellite_info.h
        sensor_gnss_relative.h
        sensor_gps.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# ----------------------------------------------------------------------------
# PX4 GPS Drivers Integration
# ----------------------------------------------------------------------------
CPMAddPackage(
    NAME px4-gpsdrivers
    GITHUB_REPOSITORY PX4/PX4-GPSDrivers
    GIT_TAG cd6f506afda9bd6e8e4645f094dcf5415f1f8be4
    SOURCE_SUBDIR src
)

# NOTE: Using file(GLOB) for external dependency sources
# CONFIGURE_DEPENDS ensures CMake re-runs if files are added/removed
file(GLOB GPS_DRIVERS_SOURCES
    CONFIGURE_DEPENDS
    "${px4-gpsdrivers_SOURCE_DIR}/src/*.c"
    "${px4-gpsdrivers_SOURCE_DIR}/src/*.cpp"
    "${px4-gpsdrivers_SOURCE_DIR}/src/*.h"
)

if(QGC_PLATFORM_OHOS)
    set(_ubx_source "${px4-gpsdrivers_SOURCE_DIR}/src/ubx.cpp")
    file(READ "${_ubx_source}" _ubx_original)
    string(REGEX MATCH "\r?\n" _ubx_newline "${_ubx_original}")
    set(_ubx_ack_loop "\twhile ((_ack_state == UBX_ACK_WAITING) && (gps_absolute_time() < time_started + timeout * 1000)) {${_ubx_newline}\t\treceive(timeout);${_ubx_newline}\t}")
    set(_ubx_ack_loop_cancel_safe "\twhile ((_ack_state == UBX_ACK_WAITING) && (gps_absolute_time() < time_started + timeout * 1000)) {${_ubx_newline}\t\tif (receive(timeout) < 0) {${_ubx_newline}\t\t\tbreak;${_ubx_newline}\t\t}${_ubx_newline}\t}")
    string(REPLACE "${_ubx_ack_loop}" "${_ubx_ack_loop_cancel_safe}" _ubx_cancel_safe "${_ubx_original}")
    string(LENGTH "${_ubx_original}" _ubx_original_length)
    string(LENGTH "${_ubx_cancel_safe}" _ubx_cancel_safe_length)
    string(LENGTH "${_ubx_ack_loop}" _ubx_anchor_length)
    string(LENGTH "${_ubx_ack_loop_cancel_safe}" _ubx_replacement_length)
    math(EXPR _ubx_expected_length "${_ubx_original_length} - ${_ubx_anchor_length} + ${_ubx_replacement_length}")
    if(NOT _ubx_cancel_safe_length EQUAL _ubx_expected_length)
        message(FATAL_ERROR "The HarmonyOS UBX ACK loop must match exactly once.")
    endif()
    set(_ubx_harmony_directory "${CMAKE_CURRENT_BINARY_DIR}/harmony-gpsdrivers")
    set(_ubx_harmony_source "${_ubx_harmony_directory}/ubx.cpp")
    file(MAKE_DIRECTORY "${_ubx_harmony_directory}")
    file(WRITE "${_ubx_harmony_source}" "${_ubx_cancel_safe}")
    list(REMOVE_ITEM GPS_DRIVERS_SOURCES "${_ubx_source}")
    list(APPEND GPS_DRIVERS_SOURCES "${_ubx_harmony_source}")
endif()

add_library(px4-gpsdrivers OBJECT ${GPS_DRIVERS_SOURCES})
target_include_directories(px4-gpsdrivers SYSTEM PRIVATE ${px4-gpsdrivers_SOURCE_DIR}/src)
target_compile_definitions(px4-gpsdrivers PRIVATE GPS_DEFINITIONS_HEADER=<${CMAKE_CURRENT_SOURCE_DIR}/definitions.h>)
target_link_libraries(px4-gpsdrivers PRIVATE Qt6::Core)
qgc_disable_dependency_warnings(px4-gpsdrivers)

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        $<TARGET_OBJECTS:px4-gpsdrivers>
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE GPS_DEFINITIONS_HEADER=<${CMAKE_CURRENT_SOURCE_DIR}/definitions.h>)
target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PRIVATE ${px4-gpsdrivers_SOURCE_DIR}/src)

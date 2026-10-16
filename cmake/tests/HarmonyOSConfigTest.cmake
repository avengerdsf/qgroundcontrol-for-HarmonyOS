cmake_minimum_required(VERSION 3.25)

set(_config "${CMAKE_CURRENT_LIST_DIR}/../HarmonyOSConfig.cmake")
if(DEFINED INVALID_ARCH)
    set(CMAKE_SYSTEM_NAME OHOS)
    set(OHOS_ARCH "${INVALID_ARCH}")
    include("${_config}")
    message(FATAL_ERROR "Invalid OHOS architecture was accepted")
endif()

function(check_platform system arch expected_ohos expected_qt)
    set(CMAKE_SYSTEM_NAME "${system}")
    set(OHOS_ARCH "${arch}")
    set(QGC_QT_MINIMUM_VERSION 6.8.3)
    set(QGC_QT_MAXIMUM_VERSION 6.8.3)
    include("${_config}")
    if(NOT QGC_PLATFORM_OHOS STREQUAL expected_ohos)
        message(FATAL_ERROR "Wrong target detection for ${system}/${arch}")
    endif()
    if(NOT QGC_QT_MINIMUM_VERSION STREQUAL expected_qt OR
       NOT QGC_QT_MAXIMUM_VERSION STREQUAL expected_qt)
        message(FATAL_ERROR "Wrong Qt range for ${system}/${arch}")
    endif()
endfunction()

check_platform(OHOS x86_64 ON 6.12.0)
check_platform(OHOS arm64-v8a ON 6.12.0)
foreach(_system Linux Windows Darwin Android)
    check_platform("${_system}" "" OFF 6.8.3)
endforeach()

foreach(_arch "riscv64" "")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DINVALID_ARCH=${_arch}" -P "${CMAKE_CURRENT_LIST_FILE}"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _output
        ERROR_VARIABLE _error
    )
    if(_result EQUAL 0 OR NOT _error MATCHES "Unsupported HarmonyOS architecture")
        message(FATAL_ERROR "Invalid architecture check failed: ${_output}${_error}")
    endif()
endforeach()

message(STATUS "HarmonyOS target and Qt range checks passed")

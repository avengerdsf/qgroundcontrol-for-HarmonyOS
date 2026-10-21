cmake_minimum_required(VERSION 3.22)
include("${CMAKE_CURRENT_LIST_DIR}/_assert.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../platform/OHOS.cmake")

if(REJECT_FLAG)
    gstreamer_validate_ohos_flags(KIND "${REJECT_KIND}" OPTIONS "${REJECT_FLAG}")
    message(FATAL_ERROR "Unsafe OHOS flag was accepted")
endif()

gstreamer_validate_ohos_flags(KIND COMPILE OPTIONS -pthread -DPCRE2_STATIC -DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_56)
gstreamer_validate_ohos_flags(KIND LINK OPTIONS -pthread -Wl,--export-dynamic)
foreach(_case IN ITEMS
    "COMPILE|-I/host/include" "COMPILE|-includehost.h" "COMPILE|-DKEY=/host"
    "COMPILE|@compile.rsp" "COMPILE|--sysroot=host" "LINK|-Wl,-rpath,/host"
    "LINK|/host/libhost.a" "LINK|-Wl,-lhost" "LINK|-Wl,--dynamic-list=host.syms"
    "LINK|-Wl,--script=host.lds" "LINK|-L/host" "LINK|@link.rsp")
    string(REPLACE "|" ";" _parts "${_case}")
    list(GET _parts 0 _kind)
    list(GET _parts 1 _flag)
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DREJECT_KIND=${_kind}" "-DREJECT_FLAG=${_flag}"
        -P "${CMAKE_CURRENT_LIST_FILE}" RESULT_VARIABLE _result ERROR_VARIABLE _err OUTPUT_VARIABLE _out)
    if(_result EQUAL 0 OR NOT "${_err}" MATCHES "unsupported OHOS pkg-config")
        message(FATAL_ERROR "ASSERT: unsafe flag '${_flag}' was not rejected: ${_out}${_err}")
    endif()
endforeach()
qgc_test_pass("OHOS pkg-config flags reject host paths, response files and linker indirection")

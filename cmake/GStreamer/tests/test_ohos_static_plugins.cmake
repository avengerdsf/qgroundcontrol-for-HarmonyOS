cmake_minimum_required(VERSION 3.22)
include("${CMAKE_CURRENT_LIST_DIR}/_assert.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../PluginPolicy.cmake")

if(TEST_MISSING_CODEC)
    gstreamer_require_static_plugins(REQUESTED coreelements libav AVAILABLE coreelements)
    message(FATAL_ERROR "Missing static codec was accepted")
endif()

gstreamer_require_static_plugins(
    REQUESTED
    coreelements
    libav
    videoconvertscale
    videoconvert
    videoscale
    AVAILABLE
    coreelements
    libav
    videoconvertscale
)
gstreamer_require_static_plugins(
    REQUESTED
    coreelements
    videoconvertscale
    AVAILABLE
    coreelements
    videoconvert
    videoscale
)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -DTEST_MISSING_CODEC=ON -P "${CMAKE_CURRENT_LIST_FILE}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
)
if(_result EQUAL 0
   OR NOT "${_err}" MATCHES "libav"
   OR "${_err}" MATCHES "was accepted"
)
    message(FATAL_ERROR "ASSERT: absent static codec did not fail with its name: ${_out}${_err}")
endif()
qgc_test_pass("static plugins accept format-conversion alternates and reject missing codecs")

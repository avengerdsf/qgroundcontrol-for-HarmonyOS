cmake_minimum_required(VERSION 3.22)
include("${CMAKE_CURRENT_LIST_DIR}/_assert.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../Helpers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../platform/OHOS.cmake")

if(TEST_REJECT_SHARED)
    set(GStreamer_USE_STATIC_LIBS OFF)
    _qgc_discover_ohos_sdk()
    message(FATAL_ERROR "Shared OHOS SDK was accepted")
endif()
if(TEST_REJECT_MISSING_ROOT)
    unset(GStreamer_ROOT_DIR CACHE)
    unset(GStreamer_ROOT_DIR)
    _qgc_discover_ohos_sdk()
    message(FATAL_ERROR "Missing OHOS SDK root was accepted")
endif()

set(_fixture "${CMAKE_CURRENT_BINARY_DIR}/ohos-sdk-fixture")
file(MAKE_DIRECTORY "${_fixture}/lib/gstreamer-1.0/pkgconfig" "${_fixture}/lib/pkgconfig"
     "${_fixture}/include/gstreamer-1.0" "${_fixture}/share/pkgconfig" "${_fixture}/etc/ssl/certs"
)
file(WRITE "${_fixture}/etc/ssl/certs/ca-certificates.crt" "configure-only CA fixture")
file(
    WRITE "${_fixture}/build-manifest.json"
    [[{
    "version":"1.28.4", "architecture":"x86_64", "abi":"x86_64",
    "defaultLibrary":"static", "glEnabled":true, "plugins":["coreelements"],
    "gioModules":["openssl"], "caBundle":"etc/ssl/certs/ca-certificates.crt"
}]]
)
set(GStreamer_ROOT_DIR "${_fixture}")
set(ENV{PKG_CONFIG_PATH} "/host/must/not/leak")
set(ENV{PKG_CONFIG_LIBDIR} "/host/lib/pkgconfig")
_qgc_discover_ohos_sdk()
qgc_test_assert_streq("OHOS static SDK" ON "${GStreamer_USE_STATIC_LIBS}")
qgc_test_assert_streq("OHOS enables GL by default" ON "${QGC_GST_OHOS_GL}")
qgc_test_assert_streq("OHOS CA resource input" "${_fixture}/etc/ssl/certs/ca-certificates.crt"
                      "${GStreamer_OHOS_CA_BUNDLE}"
)
qgc_test_assert_streq("OHOS SDK lib path" "${_fixture}/lib" "${GSTREAMER_LIB_PATH}")
qgc_test_assert_streq("Host pkg-config path removed" "" "$ENV{PKG_CONFIG_PATH}")
qgc_test_assert_streq("CMake host prefixes cannot augment target pkg-config" FALSE
                      "${PKG_CONFIG_USE_CMAKE_PREFIX_PATH}"
)
if("$ENV{PKG_CONFIG_LIBDIR}" MATCHES "/host/")
    message(FATAL_ERROR "ASSERT: OHOS SDK retained host pkg-config search path")
endif()
if(NOT "$ENV{PKG_CONFIG_LIBDIR}" MATCHES "gstreamer-1.0/pkgconfig")
    message(FATAL_ERROR "ASSERT: OHOS static-plugin pkg-config directory missing")
endif()

foreach(_mode IN ITEMS TEST_REJECT_SHARED TEST_REJECT_MISSING_ROOT)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-D${_mode}=ON" "-DGStreamer_ROOT_DIR=${_fixture}" -P "${CMAKE_CURRENT_LIST_FILE}"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE _err
    )
    if(_mode STREQUAL "TEST_REJECT_SHARED")
        set(_expected "requires GStreamer_USE_STATIC_LIBS=ON")
    else()
        set(_expected "set GStreamer_ROOT_DIR")
    endif()
    if(_result EQUAL 0 OR NOT "${_err}" MATCHES "${_expected}")
        message(FATAL_ERROR "ASSERT: ${_mode} did not reject invalid SDK: ${_out}${_err}")
    endif()
endforeach()
file(REMOVE_RECURSE "${_fixture}")
qgc_test_pass("OHOS SDK discovery and host isolation")

# QGC tests add sources to ${CMAKE_PROJECT_NAME} (test/Vehicle/, test/MissionManager/); keep this test on the same target.

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        GStreamerTest.cc
        GStreamerTest.h
        HwBuffers/common/GStreamerHwBuffersCommonTest.cc
        HwBuffers/d3d/GStreamerD3DTest.cc
        HwBuffers/dmabuf/GStreamerDmaBufTest.cc
        HwBuffers/gl/GStreamerGlTest.cc
        HwBuffers/vulkan/GStreamerVulkanTest.cc
        SourceFactory/GStreamerSourceFactoryTest.cc
        gstqgc/GStreamerGstQgcTest.cc
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_qgc_test(GStreamerTest LABELS Integration)
if(QGC_PLATFORM_OHOS AND QGC_ENABLE_GST_VIDEOSTREAMING)
    set(_gst_tls_key "${CMAKE_CURRENT_SOURCE_DIR}/tls/server-public-test-key.data")
    if(NOT EXISTS "${_gst_tls_key}")
        set(_gst_tls_key "${CMAKE_CURRENT_BINARY_DIR}/tls/server-public-test-key.data")
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tls")
        file(DOWNLOAD
            "https://raw.githubusercontent.com/GNOME/glib-networking/2.80.1/tls/tests/files/server-key-pkcs8.pem"
            "${_gst_tls_key}"
            EXPECTED_HASH SHA256=e0df67867769430ae32b6088db6158378de3d87f78e97cd3688444844318ea35
            TLS_VERIFY ON)
    endif()
    file(SHA256 "${_gst_tls_key}" _gst_tls_key_sha256)
    if(NOT _gst_tls_key_sha256 STREQUAL "e0df67867769430ae32b6088db6158378de3d87f78e97cd3688444844318ea35")
        message(FATAL_ERROR "The public GStreamer TLS test key does not match its pinned SHA256")
    endif()
    set_source_files_properties("${_gst_tls_key}" PROPERTIES QT_RESOURCE_ALIAS "tls/server-public-test-key.data")
    target_link_options(${CMAKE_PROJECT_NAME} PRIVATE "-Wl,--wrap=g_get_monotonic_time")
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE GStreamerOhosTest.cc GStreamerOhosTlsTest.cc)
    qt_add_resources(${CMAKE_PROJECT_NAME} "gstreamer_ohos_tls_tests"
        PREFIX "/GStreamerTests"
        FILES tls/ca-certificate.data tls/server-certificate.data
              tls/self-signed-certificate.data "${_gst_tls_key}")
    add_qgc_test(GStreamerOhosTest LABELS Integration)
    add_qgc_test(GStreamerOhosTlsTest LABELS Integration)
endif()

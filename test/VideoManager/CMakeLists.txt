if(QGC_FFMPEG_STREAMING)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE FFmpegVideoReceiverTest.cc FFmpegVideoReceiverTest.h)
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/FFmpegVideoReceiverTest.cc"
        TARGET_DIRECTORY ${CMAKE_PROJECT_NAME} APPEND PROPERTY
        INCLUDE_DIRECTORIES "${QGC_FFMPEG_ROOT}/include")
    qt_add_resources(${CMAKE_PROJECT_NAME} ffmpeg_test_fixture
        PREFIX "/ffmpeg-test"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}/data"
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/fixture.mp4"
    )
endif()

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
    qt_add_resources(${CMAKE_PROJECT_NAME} gstreamer_tls_test_fixtures
        PREFIX "/GStreamerTests"
        FILES tls/ca-certificate.data tls/server-certificate.data
              tls/self-signed-certificate.data "${_gst_tls_key}")
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE GStreamerVideoReceiverTest.cc GStreamerVideoReceiverTest.h)
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    get_property(_gst_test_plugins TARGET ${CMAKE_PROJECT_NAME} PROPERTY QGC_GSTREAMER_PLUGINS)
    string(JOIN "," _gst_test_plugins ${_gst_test_plugins})
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE "QGC_GST_TEST_PLUGINS=\"${_gst_test_plugins}\"")
endif()

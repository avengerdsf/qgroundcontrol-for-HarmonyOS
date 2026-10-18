# ============================================================================
# VideoManager Tests
# ============================================================================

add_subdirectory(GStreamer)

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        VideoManagerInitTest.cc
        VideoManagerInitTest.h
)

add_qgc_test(VideoManagerInitTest LABELS Unit)

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
    add_qgc_test(FFmpegVideoReceiverTest LABELS Integration VideoManager)
endif()

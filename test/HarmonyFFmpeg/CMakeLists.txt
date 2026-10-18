cmake_minimum_required(VERSION 3.25)
project(QGroundControl VERSION 5.1.5 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Multimedia Network Test)
set(QGC_FFMPEG_ROOT "" CACHE PATH "Target FFmpeg 9 install directory")
if(NOT EXISTS "${QGC_FFMPEG_ROOT}/include/libavformat/avformat.h")
    message(FATAL_ERROR "QGC_FFMPEG_ROOT must provide FFmpeg headers")
endif()
set(qgcRoot "${CMAKE_CURRENT_LIST_DIR}/../..")
if(OHOS)
    qt_add_executable(QGroundControl)
else()
    add_library(QGroundControl OBJECT)
endif()
target_sources(QGroundControl PRIVATE
    "${qgcRoot}/src/VideoManager/VideoReceiver/VideoReceiver.h"
    "${qgcRoot}/test/VideoManager/FFmpegVideoReceiverTest.cc"
    "${qgcRoot}/test/VideoManager/FFmpegVideoReceiverTest.h")
target_compile_definitions(QGroundControl PRIVATE QGC_FFMPEG_NATIVE_TEST QGC_FFMPEG_STREAMING)
target_include_directories(QGroundControl PRIVATE
    "${qgcRoot}/src/VideoManager/VideoReceiver" "${qgcRoot}/src/VideoManager/VideoReceiver/FFmpeg"
    "${qgcRoot}/test/VideoManager" "${QGC_FFMPEG_ROOT}/include")
target_link_libraries(QGroundControl PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Multimedia Qt6::Network Qt6::Test)
if(OHOS)
    add_subdirectory("${qgcRoot}/src/VideoManager/VideoReceiver/FFmpeg" ffmpeg)
    target_link_options(QGroundControl PRIVATE -Wl,--no-undefined)
    qt_add_harmonyos_permission(QGroundControl NAME "ohos.permission.INTERNET")
    qt_add_resources(QGroundControl ffmpeg_test_fixture PREFIX "/ffmpeg-test"
        BASE "${qgcRoot}/test/VideoManager/data" FILES "${qgcRoot}/test/VideoManager/data/fixture.mp4")
    set_target_properties(QGroundControl PROPERTIES
        QT_HARMONYOS_APP_BUNDLE_NAME "org.mavlink.qgc.ffmpeg.tests"
        QT_HARMONYOS_APP_NAME "QGroundControl"
        QT_HARMONYOS_APP_LABEL "QGC FFmpeg Tests"
        QT_HARMONYOS_APP_VENDOR "QGroundControl"
        QT_HARMONYOS_APP_ICON "${qgcRoot}/resources/icons/android_512x512.png"
        QT_HARMONYOS_APP_VERSION_NAME "5.1.5"
        QT_HARMONYOS_APP_VERSION_CODE 5010500
        QT_HARMONYOS_MODULE_DEVICE_TYPES "tablet;2in1;phone"
        QT_HARMONYOS_COMPATIBLE_SDK_VERSION "6.1.0(23)"
        QT_HARMONYOS_TARGET_ARCHS "${OHOS_ARCH}")
else()
    target_sources(QGroundControl PRIVATE
        "${qgcRoot}/src/VideoManager/VideoReceiver/FFmpeg/FFmpegVideoReceiver.cc"
        "${qgcRoot}/src/VideoManager/VideoReceiver/FFmpeg/FFmpegVideoReceiver.h")
endif()

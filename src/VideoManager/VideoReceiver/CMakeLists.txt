target_sources(${CMAKE_PROJECT_NAME} PRIVATE VideoReceiver.h)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(GStreamer)
add_subdirectory(QtMultimedia)

option(QGC_FFMPEG_STREAMING "Enable FFmpeg video streaming" OFF)
set(QGC_FFMPEG_ROOT "" CACHE PATH "Target FFmpeg install directory")
if(QGC_FFMPEG_STREAMING)
    add_subdirectory(FFmpeg)
endif()

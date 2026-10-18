set(QGC_PLATFORM_OHOS OFF)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "OHOS")
    return()
endif()

set(QGC_PLATFORM_OHOS ON)
if(NOT OHOS_ARCH STREQUAL "x86_64" AND NOT OHOS_ARCH STREQUAL "arm64-v8a")
    message(FATAL_ERROR "Unsupported HarmonyOS architecture: '${OHOS_ARCH}'. Use x86_64 or arm64-v8a.")
endif()

set(QGC_QT_MINIMUM_VERSION "6.12.0")
set(QGC_QT_MAXIMUM_VERSION "6.12.0")

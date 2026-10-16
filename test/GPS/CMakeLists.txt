target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        GpsTest.cc
        GpsTest.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(QGC_PLATFORM_OHOS)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE HarmonyNmeaTest.cc HarmonyNmeaTest.h)
    if(NOT QGC_NO_SERIAL_LINK)
        target_sources(${CMAKE_PROJECT_NAME} PRIVATE HarmonyRtkFirmwareTest.cc HarmonyRtkFirmwareTest.h)
    endif()
endif()

# ============================================================================
# Joystick Module
# Game controller and joystick input support
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        Joystick.cc
        Joystick.h
        JoystickManager.cc
        JoystickManager.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(QGC_PLATFORM_OHOS)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE HarmonyJoystickInput.cc HarmonyJoystickInput.h)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE deviceinfo_ndk.z)
endif()

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        JoystickSDL.cc
        JoystickSDL.h
)

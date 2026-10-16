cmake_minimum_required(VERSION 3.22)
project(HarmonyJoystickInputNativeTest LANGUAGES CXX)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 REQUIRED COMPONENTS Core Test)
get_filename_component(QGC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
add_executable(HarmonyJoystickInputNativeTest
    "${CMAKE_CURRENT_SOURCE_DIR}/../HarmonyJoystickInputTest.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../HarmonyJoystickInputTest.h"
    "${QGC_ROOT}/src/Joystick/HarmonyJoystickInput.cc"
)
target_include_directories(HarmonyJoystickInputNativeTest PRIVATE
    "${QGC_ROOT}/src/Joystick" "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${QGC_ROOT}/tmp/cache/qgc-v5.0.8-deps/SDL2/include"
)
target_compile_definitions(HarmonyJoystickInputNativeTest PRIVATE QGC_HARMONY_JOYSTICK_NATIVE_TEST SDL_MAIN_HANDLED)
target_link_libraries(HarmonyJoystickInputNativeTest PRIVATE Qt6::Core Qt6::Test
    "${QGC_ROOT}/tmp/build-v5.0.8-windows-debug/Debug/lib/libSDL2d.a"
    winmm imm32 version setupapi
)

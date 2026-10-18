cmake_minimum_required(VERSION 3.25)
project(HarmonyJoystickInputNativeTest LANGUAGES CXX)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 20)
find_package(Qt6 REQUIRED COMPONENTS Core Test)
find_package(SDL3 REQUIRED CONFIG)
get_filename_component(QGC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
add_executable(HarmonyJoystickInputNativeTest
    "${CMAKE_CURRENT_SOURCE_DIR}/../HarmonyJoystickInputTest.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../HarmonyJoystickInputTest.h"
    "${QGC_ROOT}/src/Joystick/HarmonyJoystickInput.cc"
)
target_include_directories(HarmonyJoystickInputNativeTest PRIVATE
    "${QGC_ROOT}/src/Joystick" "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_compile_definitions(HarmonyJoystickInputNativeTest PRIVATE
    QGC_HARMONY_JOYSTICK_NATIVE_TEST SDL_MAIN_HANDLED)
target_link_libraries(HarmonyJoystickInputNativeTest PRIVATE Qt6::Core Qt6::Test SDL3::SDL3)

enable_testing()
add_test(NAME HarmonyJoystickInputNativeTest
    COMMAND HarmonyJoystickInputNativeTest -o "${CMAKE_BINARY_DIR}/result.txt,txt")

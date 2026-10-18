cmake_minimum_required(VERSION 3.25)
project(HarmonyStartupTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Test)

add_executable(HarmonyStartupTest
    AudioStartupTest.cc
    ../../src/Utilities/Audio/AudioOutput.cc
    ../../src/Utilities/Audio/AudioOutput.h
    stubs/Fact.h
    stubs/FakeTextToSpeech.h
)
target_compile_definitions(HarmonyStartupTest PRIVATE Q_OS_HARMONY)
target_include_directories(HarmonyStartupTest PRIVATE
    stubs
    ../../src/Utilities/Audio
)
target_link_libraries(HarmonyStartupTest PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Test)

enable_testing()
add_test(NAME HarmonyStartupTest COMMAND HarmonyStartupTest
    -o "${CMAKE_CURRENT_BINARY_DIR}/audio-startup-results.txt,txt")
set_tests_properties(HarmonyStartupTest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1"
)

add_executable(AudioSynchronousTest
    AudioSynchronousTest.cc
    ../../src/Utilities/Audio/AudioOutput.cc
    ../../src/Utilities/Audio/AudioOutput.h
    stubs/Fact.h
    stubs/FakeTextToSpeech.h
)
target_include_directories(AudioSynchronousTest PRIVATE stubs ../../src/Utilities/Audio)
target_link_libraries(AudioSynchronousTest PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Test)
add_test(NAME AudioSynchronousTest COMMAND AudioSynchronousTest
    -o "${CMAKE_CURRENT_BINARY_DIR}/audio-synchronous-results.txt,txt")
set_tests_properties(AudioSynchronousTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

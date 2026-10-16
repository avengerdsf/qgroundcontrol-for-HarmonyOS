cmake_minimum_required(VERSION 3.25)
project(LanguageSwitchTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml LinguistTools)

set(application_file "${CMAKE_CURRENT_LIST_DIR}/../../src/QGCApplication.cc")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${application_file}")
file(READ "${application_file}" application_source)
string(FIND "${application_source}" "void QGCApplication::setLanguage()" function_start)
string(FIND "${application_source}" "QGCApplication::~QGCApplication()" function_end)
if(function_start LESS 0 OR function_end LESS function_start)
    message(FATAL_ERROR "Cannot locate QGCApplication::setLanguage")
endif()
math(EXPR function_length "${function_end} - ${function_start}")
string(SUBSTRING "${application_source}" ${function_start} ${function_length} language_function)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/SetLanguage.inc" "${language_function}")

add_executable(LanguageSwitchTest LanguageSwitchTest.cc)
target_include_directories(LanguageSwitchTest PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(LanguageSwitchTest PRIVATE Qt6::Core Qt6::Gui Qt6::Qml)

set(ts_files
    "${CMAKE_CURRENT_LIST_DIR}/../../translations/qgc_source_zh_CN.ts"
    "${CMAKE_CURRENT_LIST_DIR}/../../translations/qgc_json_zh_CN.ts"
)
set_source_files_properties(${ts_files} PROPERTIES OUTPUT_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/i18n")
qt_add_translations(LanguageSwitchTest TS_FILES ${ts_files} RESOURCE_PREFIX "/")

enable_testing()
add_test(NAME LanguageSwitchTest COMMAND LanguageSwitchTest)

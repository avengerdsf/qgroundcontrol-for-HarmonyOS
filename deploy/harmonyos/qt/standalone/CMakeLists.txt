cmake_minimum_required(VERSION 3.24)
project(QOhosPlatformPluginStandalone VERSION 6.12.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if(NOT QT_SOURCE_ROOT OR NOT QOHOS_DEPENDENCY_INCLUDE_DIR)
    message(FATAL_ERROR "QT_SOURCE_ROOT and QOHOS_DEPENDENCY_INCLUDE_DIR are required")
endif()
find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS Core CorePrivate Gui GuiPrivate OpenGL OpenGLPrivate)
set(plugin_source "${QT_SOURCE_ROOT}/qtbase/src/plugins/platforms/ohos")
foreach(header napi.h fontconfig/fontconfig.h)
    if(NOT EXISTS "${QOHOS_DEPENDENCY_INCLUDE_DIR}/${header}")
        message(FATAL_ERROR "Missing platform plugin dependency header: ${header}")
    endif()
endforeach()

# The supplied ARM64 QtGui exports the Fontconfig API used by its platform plugin.
get_target_property(gui_library Qt6::Gui IMPORTED_LOCATION_RELEASE)
find_program(ohos_nm llvm-nm HINTS "${OHOS_SDK_ROOT}/native/llvm/bin" NO_DEFAULT_PATH REQUIRED)
execute_process(COMMAND "${ohos_nm}" -D --defined-only "${gui_library}"
    RESULT_VARIABLE nm_result OUTPUT_VARIABLE gui_exports ERROR_VARIABLE nm_error)
if(NOT nm_result EQUAL 0)
    message(FATAL_ERROR "Cannot inspect ARM64 QtGui exports: ${nm_error}")
endif()
foreach(symbol FcInit FcConfigGetCurrent FcConfigAppFontAddDir FcConfigBuildFonts)
    if(NOT gui_exports MATCHES "[ \t]${symbol}@@Qt_6([\r\n]|$)")
        message(FATAL_ERROR "The supplied QtGui does not export ${symbol}@@Qt_6")
    endif()
endforeach()
add_library(NodeAddonApi::NodeAddonApi INTERFACE IMPORTED)
set_target_properties(NodeAddonApi::NodeAddonApi PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${QOHOS_DEPENDENCY_INCLUDE_DIR}")
add_library(WrapFontconfig::WrapFontconfig INTERFACE IMPORTED)
set_target_properties(WrapFontconfig::WrapFontconfig PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${QOHOS_DEPENDENCY_INCLUDE_DIR}"
    INTERFACE_LINK_LIBRARIES Qt6::Gui)

# Consume the upstream source/dependency lists with the public installed-Qt plugin builder.
function(qt_find_package)
    find_package(${ARGV} REQUIRED)
endfunction()
function(qt_internal_add_plugin target)
    cmake_parse_arguments(arg "EXCEPTIONS" "OUTPUT_NAME;PLUGIN_TYPE"
        "DEFAULT_IF;SOURCES;INCLUDE_DIRECTORIES;DEFINES;LIBRARIES" ${ARGN})
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "Unsupported upstream plugin arguments: ${arg_UNPARSED_ARGUMENTS}")
    endif()
    qt6_add_plugin(${target} SHARED CLASS_NAME ${target}
        OUTPUT_NAME ${arg_OUTPUT_NAME} PLUGIN_TYPE ${arg_PLUGIN_TYPE} ${arg_SOURCES})
    target_include_directories(${target} PRIVATE ${arg_INCLUDE_DIRECTORIES})
    target_compile_definitions(${target} PRIVATE ${arg_DEFINES}
        QT_EXPLICIT_QFILE_CONSTRUCTION_FROM_PATH QT_LEAN_HEADERS=1
        QT_NO_DISCONNECT_CONST_CONNECTION=1 QT_NO_FOREACH QT_NO_JAVA_STYLE_ITERATORS
        QT_NO_NARROWING_CONVERSIONS_IN_CONNECT QT_NO_QASCONST QT_NO_QEXCHANGE
        QT_NO_QSNPRINTF QT_NO_SINGLE_ARGUMENT_QHASH_OVERLOAD=1
        QT_NO_STD_FORMAT_SUPPORT QT_USE_QSTRINGBUILDER)
    target_link_libraries(${target} PRIVATE ${arg_LIBRARIES})
    if(arg_EXCEPTIONS)
        target_compile_options(${target} PRIVATE -fexceptions)
    endif()
    set_target_properties(${target} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/platforms"
        BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "$ORIGIN")
    target_link_options(${target} PRIVATE -Wl,--no-undefined
        "-Wl,--version-script,${QOHOS_VERSION_SCRIPT}")
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${QOHOS_VERSION_SCRIPT}")
endfunction()
function(qt_internal_extend_target target)
    cmake_parse_arguments(arg "" "CONDITION" "SOURCES;DEFINES" ${ARGN})
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "Unsupported upstream extension arguments: ${arg_UNPARSED_ARGUMENTS}")
    endif()
    if(${arg_CONDITION})
        target_sources(${target} PRIVATE ${arg_SOURCES})
        target_compile_definitions(${target} PRIVATE ${arg_DEFINES})
    endif()
endfunction()
set(QOHOS_VERSION_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/qohos.version")
set(QT_FEATURE_widgets OFF)
add_subdirectory("${plugin_source}" ohos)
